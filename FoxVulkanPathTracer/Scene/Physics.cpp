#include "FoxRenderer.h"

#include <cstdarg>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/ContactListener.h>

namespace Fox {

	namespace Scene {

		namespace {

			// --- Collision layers ---
			// Jolt's object layer is the user's layer (0..15), plus 16 for bodies that do not move: what does not move
			// never collides with itself, and the matrix decides the rest
			constexpr int LAYERS = PhysicsWorld::LAYER_COUNT;
			JPH::ObjectLayer ObjectLayer(int layer, bool moving) {
				return static_cast<JPH::ObjectLayer>(std::clamp(layer, 0, LAYERS - 1) + (moving ? 0 : LAYERS));
			}
			int UserLayer(JPH::ObjectLayer layer) { return static_cast<int>(layer) % LAYERS; }
			bool IsMoving(JPH::ObjectLayer layer) { return layer < LAYERS; }

			namespace BroadPhaseLayers {
				constexpr JPH::BroadPhaseLayer NON_MOVING(0);
				constexpr JPH::BroadPhaseLayer MOVING(1);
				constexpr JPH::uint COUNT = 2;
			}

			class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
			public:
				JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::COUNT; }
				JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
					return IsMoving(layer) ? BroadPhaseLayers::MOVING : BroadPhaseLayers::NON_MOVING;
				}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
				const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
					return layer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
				}
#endif
			};

			class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
			public:
				bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override {
					return IsMoving(layer) || broadPhaseLayer == BroadPhaseLayers::MOVING;
				}
			};

			// The user's matrix (read by the physics threads; changed only between steps)
			class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
			public:
				uint32_t masks[LAYERS];
				ObjectLayerPairFilter() {
					for (uint32_t& mask : masks) mask = PhysicsWorld::ALL_LAYERS;
				}
				bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
					if (!IsMoving(a) && !IsMoving(b)) {
						return false;
					}
					return ((masks[UserLayer(a)] >> UserLayer(b)) & 1u) != 0;
				}
			};

			// Queries: the layers in a mask
			class LayerMaskFilter final : public JPH::ObjectLayerFilter {
			public:
				explicit LayerMaskFilter(uint32_t mask) : mask(mask) {}
				bool ShouldCollide(JPH::ObjectLayer layer) const override { return ((mask >> UserLayer(layer)) & 1u) != 0; }
			private:
				uint32_t mask;
			};

			// Queries: not the ignored body, and triggers only when asked
			class QueryBodyFilter final : public JPH::BodyFilter {
			public:
				QueryBodyFilter(uint32_t ignore, bool triggers) : ignore(ignore), triggers(triggers) {}
				bool ShouldCollide(const JPH::BodyID& id) const override { return id.GetIndexAndSequenceNumber() != ignore; }
				bool ShouldCollideLocked(const JPH::Body& body) const override { return triggers || !body.IsSensor(); }
			private:
				uint32_t ignore;
				bool triggers;
			};

			// Contacts beginning and ending, from the physics threads. A pair of bodies touching in several places
			// (sub shapes: a mesh's triangles) is counted, so it begins once and ends once. Jolt drops the contacts of
			// bodies that fall asleep and makes them again when they wake: those pairs are still touching, so they
			// neither end nor begin (Take sorts the ended pairs out, on the main thread).
			class ContactCollector final : public JPH::ContactListener {
			public:
				static uint64_t Key(JPH::BodyID a, JPH::BodyID b) {
					uint32_t x = a.GetIndexAndSequenceNumber(), y = b.GetIndexAndSequenceNumber();
					if (x > y) std::swap(x, y);
					return (static_cast<uint64_t>(x) << 32) | y;
				}

				void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override {
					PhysicsWorld::ContactEvent event;
					event.begin = true;
					event.trigger = body1.IsSensor() || body2.IsSensor();
					event.bodyA = body1.GetID().GetIndexAndSequenceNumber();
					event.bodyB = body2.GetID().GetIndexAndSequenceNumber();
					const JPH::RVec3 point = manifold.GetWorldSpaceContactPointOn1(0);
					event.point = glm::vec3(static_cast<float>(point.GetX()), static_cast<float>(point.GetY()), static_cast<float>(point.GetZ()));
					event.normal = ToGlmVec(manifold.mWorldSpaceNormal);
					// How fast they close in (the normal runs from body 1 to body 2): 0 when they are moving apart
					const JPH::Vec3 relative = body2.GetLinearVelocity() - body1.GetLinearVelocity();
					event.speed = std::max(0.0f, -relative.Dot(manifold.mWorldSpaceNormal));
					const uint64_t key = Key(body1.GetID(), body2.GetID());
					std::lock_guard<std::mutex> lock(mutex);
					Pair& pair = pairs[key];
					pair.trigger = event.trigger;
					if (pair.count++ == 0) {
						// Woken up still touching: not a new contact
						if (sleeping.erase(key) == 0) {
							events.push_back(event);
						}
					}
				}

				void OnContactRemoved(const JPH::SubShapeIDPair& subShapes) override {
					std::lock_guard<std::mutex> lock(mutex);
					auto found = pairs.find(Key(subShapes.GetBody1ID(), subShapes.GetBody2ID()));
					if (found == pairs.end() || --found->second.count > 0) {
						return;
					}
					// Ended, or asleep: Take decides
					ended.push_back({ found->first, subShapes.GetBody1ID(), subShapes.GetBody2ID(), found->second.trigger });
					pairs.erase(found);
				}

				// A body leaves the world: whatever it touched ends (asleep or not)
				void Forget(JPH::BodyID body) {
					std::lock_guard<std::mutex> lock(mutex);
					const uint32_t id = body.GetIndexAndSequenceNumber();
					auto involves = [id](uint64_t key) {
						return static_cast<uint32_t>(key >> 32) == id || static_cast<uint32_t>(key & 0xffffffffu) == id;
					};
					auto end = [&](uint64_t key, bool trigger) {
						PhysicsWorld::ContactEvent event;
						event.begin = false;
						event.trigger = trigger;
						event.bodyA = static_cast<uint32_t>(key >> 32);
						event.bodyB = static_cast<uint32_t>(key & 0xffffffffu);
						events.push_back(event);
					};
					for (auto it = pairs.begin(); it != pairs.end();) {
						if (involves(it->first)) {
							end(it->first, it->second.trigger);
							it = pairs.erase(it);
						}
						else {
							++it;
						}
					}
					for (auto it = sleeping.begin(); it != sleeping.end();) {
						if (involves(it->first)) {
							end(it->first, it->second);
							it = sleeping.erase(it);
						}
						else {
							++it;
						}
					}
				}

				// The events since the last call. A pair that stopped touching while neither body is awake fell asleep
				// touching: it is kept (no end), and waking it is no begin.
				std::vector<PhysicsWorld::ContactEvent> Take(const JPH::BodyInterface& bodies) {
					std::lock_guard<std::mutex> lock(mutex);
					for (const Ended& pair : ended) {
						const bool both = bodies.IsAdded(pair.a) && bodies.IsAdded(pair.b);
						if (both && !bodies.IsActive(pair.a) && !bodies.IsActive(pair.b) && !pairs.count(pair.key)) {
							sleeping[pair.key] = pair.trigger;
							continue;
						}
						if (pairs.count(pair.key)) {
							continue; // Touching again already (in the same frame): it never ended
						}
						PhysicsWorld::ContactEvent event;
						event.begin = false;
						event.trigger = pair.trigger;
						event.bodyA = pair.a.GetIndexAndSequenceNumber();
						event.bodyB = pair.b.GetIndexAndSequenceNumber();
						events.push_back(event);
					}
					ended.clear();
					std::vector<PhysicsWorld::ContactEvent> taken;
					taken.swap(events);
					return taken;
				}

				void Clear() {
					std::lock_guard<std::mutex> lock(mutex);
					pairs.clear();
					sleeping.clear();
					ended.clear();
					events.clear();
				}

			private:
				struct Pair {
					int count = 0;
					bool trigger = false;
				};
				struct Ended {
					uint64_t key;
					JPH::BodyID a, b;
					bool trigger;
				};
				std::vector<Ended> ended;
				std::unordered_map<uint64_t, bool> sleeping; // Pairs asleep while touching: whether a trigger
				static glm::vec3 ToGlmVec(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
				std::mutex mutex;
				std::unordered_map<uint64_t, Pair> pairs;
				std::vector<PhysicsWorld::ContactEvent> events;
			};

			void TraceImpl(const char* format, ...) {
				va_list list;
				va_start(list, format);
				char buffer[1024];
				vsnprintf(buffer, sizeof(buffer), format, list);
				va_end(list);
				std::cout << "Jolt: " << buffer << "\n";
			}

			// Jolt's allocator, factory and types: once per process
			void InitializeJolt() {
				static std::once_flag once;
				std::call_once(once, [] {
					JPH::RegisterDefaultAllocator();
					JPH::Trace = TraceImpl;
					JPH::Factory::sInstance = new JPH::Factory();
					JPH::RegisterTypes();
				});
			}

			JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
			JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }
			glm::vec3 ToGlm(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
			glm::quat ToGlm(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }
		}

		struct PhysicsWorld::Impl {
			BroadPhaseLayerInterface broadPhaseLayers;
			ObjectVsBroadPhaseLayerFilter objectVsBroadPhase;
			ObjectLayerPairFilter objectPairs;
			ContactCollector contacts;
			std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
			std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
			std::unique_ptr<JPH::PhysicsSystem> system;
			std::vector<JPH::BodyID> bodies; // Everything added, the ground included
			std::unordered_map<std::string, JPH::RefConst<JPH::Shape>> shapeCache;
			float accumulated = 0.0f;

			JPH::BodyInterface& Bodies() { return system->GetBodyInterface(); }
			const JPH::BodyInterface& Bodies() const { return system->GetBodyInterface(); }

			void CreateSystem() {
				system = std::make_unique<JPH::PhysicsSystem>();
				const JPH::uint maxBodies = 65536, mutexes = 0, maxPairs = 65536, maxContacts = 16384;
				system->Init(maxBodies, mutexes, maxPairs, maxContacts, broadPhaseLayers, objectVsBroadPhase, objectPairs);
				system->SetGravity(JPH::Vec3(0.0f, -GRAVITY, 0.0f));
				system->SetContactListener(&contacts);
				contacts.Clear();
				bodies.clear();
				accumulated = 0.0f;
			}

			JPH::BodyID Add(const JPH::BodyCreationSettings& settings, JPH::EActivation activation) {
				const JPH::BodyID id = Bodies().CreateAndAddBody(settings, activation);
				if (!id.IsInvalid()) {
					bodies.push_back(id);
				}
				return id;
			}
		};

		PhysicsWorld::PhysicsWorld() : impl(std::make_unique<Impl>()) {
			InitializeJolt();
			impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(32 * 1024 * 1024);
			const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
			impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
			impl->CreateSystem();
		}

		PhysicsWorld::~PhysicsWorld() {
			Reset();
		}

		void PhysicsWorld::Reset() {
			if (!impl->bodies.empty()) {
				JPH::BodyInterface& bodies = impl->Bodies();
				bodies.RemoveBodies(impl->bodies.data(), static_cast<int>(impl->bodies.size()));
				bodies.DestroyBodies(impl->bodies.data(), static_cast<int>(impl->bodies.size()));
			}
			impl->CreateSystem();
		}

		void PhysicsWorld::ClearShapeCache() {
			impl->shapeCache.clear();
		}

		void PhysicsWorld::AddGroundPlane(float height) {
			// A slab 4 km wide whose top is the ground
			JPH::BodyCreationSettings settings(new JPH::BoxShapeSettings(JPH::Vec3(2000.0f, 1.0f, 2000.0f)),
				JPH::RVec3(0.0f, height - 1.0f, 0.0f), JPH::Quat::sIdentity(), JPH::EMotionType::Static, ObjectLayer(GROUND_LAYER, false));
			settings.mFriction = 0.7f;
			impl->Add(settings, JPH::EActivation::DontActivate);
		}

		void PhysicsWorld::AddHeightField(const std::vector<float>& heights, uint32_t samples, const glm::vec2& corner, float spacing) {
			JPH::HeightFieldShapeSettings shape(heights.data(), JPH::Vec3(corner.x, 0.0f, corner.y), JPH::Vec3(spacing, 1.0f, spacing), samples);
			shape.mBlockSize = 4;
			JPH::Shape::ShapeResult result = shape.Create();
			if (result.HasError()) {
				std::cout << "Physics: the terrain's height field failed (" << result.GetError() << "), using a ground plane\n";
				float lowest = 1e9f;
				for (float h : heights) {
					lowest = std::min(lowest, h);
				}
				AddGroundPlane(lowest);
				return;
			}
			JPH::BodyCreationSettings settings(result.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, ObjectLayer(GROUND_LAYER, false));
			settings.mFriction = 0.7f;
			impl->Add(settings, JPH::EActivation::DontActivate);
		}

		uint32_t PhysicsWorld::AddBody(const BodyDesc& desc, std::string& error) {
			// The shape, in the body's space
			JPH::RefConst<JPH::Shape> shape;
			const bool cached = !desc.cacheKey.empty() && (desc.shape == ShapeType::ConvexHull || desc.shape == ShapeType::Mesh);
			if (cached) {
				auto found = impl->shapeCache.find(desc.cacheKey);
				if (found != impl->shapeCache.end()) {
					shape = found->second;
				}
			}
			if (!shape) {
				JPH::Shape::ShapeResult result;
				switch (desc.shape) {
				case ShapeType::Box: {
					const glm::vec3 half = glm::max(desc.halfExtents, glm::vec3(0.01f));
					JPH::BoxShapeSettings settings(ToJolt(half), std::min(0.05f, 0.5f * std::min({ half.x, half.y, half.z })));
					settings.SetDensity(desc.density);
					result = settings.Create();
					break;
				}
				case ShapeType::Sphere: {
					JPH::SphereShapeSettings settings(std::max(desc.radius, 0.005f));
					settings.SetDensity(desc.density);
					result = settings.Create();
					break;
				}
				case ShapeType::Capsule: {
					JPH::CapsuleShapeSettings settings(std::max(desc.halfHeight, 0.005f), std::max(desc.radius, 0.005f));
					settings.SetDensity(desc.density);
					result = settings.Create();
					break;
				}
				case ShapeType::ConvexHull: {
					// Many points only slow the hull down: every n-th will do
					JPH::Array<JPH::Vec3> points;
					const size_t stride = std::max<size_t>(1, desc.points.size() / 20000);
					for (size_t i = 0; i < desc.points.size(); i += stride) {
						points.push_back(ToJolt(desc.points[i]));
					}
					JPH::ConvexHullShapeSettings settings(points, 0.02f);
					settings.SetDensity(desc.density);
					result = settings.Create();
					break;
				}
				case ShapeType::Mesh: {
					JPH::VertexList vertices;
					vertices.reserve(desc.points.size());
					for (const glm::vec3& p : desc.points) {
						vertices.push_back(JPH::Float3(p.x, p.y, p.z));
					}
					JPH::IndexedTriangleList triangles;
					triangles.reserve(desc.indices.size() / 3);
					for (size_t i = 0; i + 2 < desc.indices.size(); i += 3) {
						triangles.push_back(JPH::IndexedTriangle(desc.indices[i], desc.indices[i + 1], desc.indices[i + 2], 0));
					}
					JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
					result = settings.Create();
					break;
				}
				}
				if (result.HasError()) {
					error = result.GetError().c_str();
					return INVALID_BODY;
				}
				shape = result.Get();
				if (cached) {
					impl->shapeCache[desc.cacheKey] = shape;
				}
			}
			if (glm::dot(desc.center, desc.center) > 1e-10f) {
				JPH::Shape::ShapeResult moved = JPH::RotatedTranslatedShapeSettings(ToJolt(desc.center), JPH::Quat::sIdentity(), shape).Create();
				if (moved.HasError()) {
					error = moved.GetError().c_str();
					return INVALID_BODY;
				}
				shape = moved.Get();
			}

			// A triangle mesh has no inside: it cannot be pushed around, only placed or moved
			Motion motion = desc.motion;
			if (desc.shape == ShapeType::Mesh && motion == Motion::Dynamic) {
				motion = Motion::Kinematic;
			}
			const JPH::EMotionType motionType = motion == Motion::Static ? JPH::EMotionType::Static
				: motion == Motion::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
			JPH::BodyCreationSettings settings(shape, JPH::RVec3(desc.position.x, desc.position.y, desc.position.z), ToJolt(desc.rotation),
				motionType, ObjectLayer(desc.layer, motion != Motion::Static));
			settings.mIsSensor = desc.trigger;
			// A trigger that does not move notices static and kinematic bodies too (not just dynamic ones)
			settings.mCollideKinematicVsNonDynamic = desc.trigger;
			settings.mFriction = desc.friction;
			settings.mRestitution = desc.restitution;
			settings.mLinearDamping = desc.linearDamping;
			settings.mAngularDamping = desc.angularDamping;
			settings.mUserData = desc.user;
			settings.mMotionQuality = desc.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
			if (motion == Motion::Dynamic && desc.mass > 0.0f) {
				settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
				settings.mMassPropertiesOverride.mMass = desc.mass;
			}
			const JPH::BodyID id = impl->Add(settings, motion == Motion::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
			if (id.IsInvalid()) {
				error = "too many bodies";
				return INVALID_BODY;
			}
			return id.GetIndexAndSequenceNumber();
		}

		void PhysicsWorld::RemoveBody(uint32_t body) {
			const JPH::BodyID id(body);
			auto found = std::find(impl->bodies.begin(), impl->bodies.end(), id);
			if (found == impl->bodies.end()) {
				return;
			}
			impl->Bodies().RemoveBody(id);
			impl->Bodies().DestroyBody(id);
			impl->bodies.erase(found);
			impl->contacts.Forget(id);
		}

		PhysicsWorld::BodyInfo PhysicsWorld::GetBodyInfo(uint32_t body) const {
			BodyInfo info;
			JPH::BodyLockRead lock(impl->system->GetBodyLockInterface(), JPH::BodyID(body));
			if (!lock.Succeeded()) {
				return info;
			}
			const JPH::Body& b = lock.GetBody();
			info.volume = b.GetShape()->GetVolume();
			if (b.IsDynamic() && b.GetMotionProperties()->GetInverseMass() > 0.0f) {
				info.mass = 1.0f / b.GetMotionProperties()->GetInverseMass();
			}
			const JPH::AABox bounds = b.GetShape()->GetLocalBounds();
			const JPH::Vec3 size = bounds.GetSize();
			info.extent = 0.5f * std::max(size.GetX(), size.GetZ());
			return info;
		}

		void PhysicsWorld::Optimize() {
			impl->system->OptimizeBroadPhase();
		}

		int PhysicsWorld::Step(float deltaTime, const std::function<void(float)>& beforeStep) {
			// A long frame (a hitch, the scene rebuilding) is not caught up on: at most 4 steps a frame
			impl->accumulated = std::min(impl->accumulated + std::max(deltaTime, 0.0f), 4.0f * STEP);
			int steps = 0;
			while (impl->accumulated >= STEP) {
				if (beforeStep) {
					beforeStep(STEP);
				}
				impl->system->Update(STEP, 1, impl->tempAllocator.get(), impl->jobSystem.get());
				impl->accumulated -= STEP;
				steps++;
			}
			return steps;
		}

		float PhysicsWorld::Interpolation() const {
			return std::clamp(impl->accumulated / STEP, 0.0f, 1.0f);
		}

		void PhysicsWorld::GetTransform(uint32_t body, glm::vec3& position, glm::quat& rotation) const {
			JPH::RVec3 p;
			JPH::Quat q;
			impl->Bodies().GetPositionAndRotation(JPH::BodyID(body), p, q);
			position = glm::vec3(static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ()));
			rotation = ToGlm(q);
		}

		void PhysicsWorld::SetTransform(uint32_t body, const glm::vec3& position, const glm::quat& rotation) {
			impl->Bodies().SetPositionAndRotation(JPH::BodyID(body), JPH::RVec3(position.x, position.y, position.z), ToJolt(rotation),
				JPH::EActivation::Activate);
		}

		glm::vec3 PhysicsWorld::GetLinearVelocity(uint32_t body) const {
			return ToGlm(impl->Bodies().GetLinearVelocity(JPH::BodyID(body)));
		}

		glm::vec3 PhysicsWorld::GetAngularVelocity(uint32_t body) const {
			return ToGlm(impl->Bodies().GetAngularVelocity(JPH::BodyID(body)));
		}

		void PhysicsWorld::AddImpulse(uint32_t body, const glm::vec3& linear, const glm::vec3& angular) {
			JPH::BodyInterface& bodies = impl->Bodies();
			const JPH::BodyID id(body);
			if (bodies.GetMotionType(id) != JPH::EMotionType::Dynamic) {
				return;
			}
			if (glm::dot(linear, linear) > 0.0f) {
				bodies.AddImpulse(id, ToJolt(linear));
			}
			if (glm::dot(angular, angular) > 0.0f) {
				bodies.AddAngularImpulse(id, ToJolt(angular));
			}
			bodies.ActivateBody(id);
		}

		bool PhysicsWorld::CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, uint32_t ignore, RayHit& hit) const {
			QueryFilter filter;
			filter.ignore = ignore;
			return CastRay(origin, direction, maxDistance, filter, hit);
		}

		namespace {
			glm::vec3 ToGlmPoint(JPH::RVec3Arg p) {
				return glm::vec3(static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ()));
			}

			JPH::RefConst<JPH::Shape> MakeQueryShape(const PhysicsWorld::QueryShape& shape) {
				switch (shape.type) {
				case PhysicsWorld::QueryShape::Type::Box: {
					const glm::vec3 half = glm::max(shape.halfExtents, glm::vec3(0.001f));
					return new JPH::BoxShape(JPH::Vec3(half.x, half.y, half.z), std::min(0.05f, 0.5f * std::min({ half.x, half.y, half.z })));
				}
				case PhysicsWorld::QueryShape::Type::Capsule:
					return new JPH::CapsuleShape(std::max(shape.halfHeight, 0.001f), std::max(shape.radius, 0.001f));
				default:
					return new JPH::SphereShape(std::max(shape.radius, 0.001f));
				}
			}
		}

		// The hit's normal and user data, from its body
		static void CompleteHit(const JPH::PhysicsSystem& system, JPH::BodyID body, const JPH::SubShapeID& subShape, JPH::RVec3Arg point,
			PhysicsWorld::RayHit& hit, bool normalFromSurface) {
			hit.body = body.GetIndexAndSequenceNumber();
			hit.point = ToGlmPoint(point);
			JPH::BodyLockRead lock(system.GetBodyLockInterface(), body);
			if (lock.Succeeded()) {
				hit.user = lock.GetBody().GetUserData();
				if (normalFromSurface) {
					hit.normal = ToGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(subShape, point));
				}
			}
		}

		bool PhysicsWorld::CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const QueryFilter& filter, RayHit& hit) const {
			const JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), ToJolt(direction * maxDistance));
			JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
			const LayerMaskFilter layers(filter.layerMask);
			const QueryBodyFilter bodies(filter.ignore, filter.triggers);
			impl->system->GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings(), collector, {}, layers, bodies);
			if (!collector.HadHit()) {
				return false;
			}
			const JPH::RayCastResult& result = collector.mHit;
			hit.distance = result.mFraction * maxDistance;
			CompleteHit(*impl->system, result.mBodyID, result.mSubShapeID2, ray.GetPointOnRay(result.mFraction), hit, true);
			return true;
		}

		void PhysicsWorld::CastRayAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const QueryFilter& filter,
			std::vector<RayHit>& hits) const {
			hits.clear();
			const JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), ToJolt(direction * maxDistance));
			JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
			const LayerMaskFilter layers(filter.layerMask);
			const QueryBodyFilter bodies(filter.ignore, filter.triggers);
			impl->system->GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings(), collector, {}, layers, bodies);
			collector.Sort();
			for (const JPH::RayCastResult& result : collector.mHits) {
				// One hit per body: the nearest (a ray through a mesh meets several of its triangles)
				if (std::any_of(hits.begin(), hits.end(), [&](const RayHit& h) { return h.body == result.mBodyID.GetIndexAndSequenceNumber(); })) {
					continue;
				}
				RayHit hit;
				hit.distance = result.mFraction * maxDistance;
				CompleteHit(*impl->system, result.mBodyID, result.mSubShapeID2, ray.GetPointOnRay(result.mFraction), hit, true);
				hits.push_back(hit);
			}
		}

		bool PhysicsWorld::CastShape(const QueryShape& shape, const glm::vec3& origin, const glm::quat& rotation, const glm::vec3& direction,
			float maxDistance, const QueryFilter& filter, RayHit& hit) const {
			const JPH::RefConst<JPH::Shape> queryShape = MakeQueryShape(shape);
			const JPH::RMat44 start = JPH::RMat44::sRotationTranslation(ToJolt(rotation), JPH::RVec3(origin.x, origin.y, origin.z));
			const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(queryShape, JPH::Vec3::sReplicate(1.0f), start, ToJolt(direction * maxDistance));
			JPH::ShapeCastSettings settings;
			settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
			JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
			const LayerMaskFilter layers(filter.layerMask);
			const QueryBodyFilter bodies(filter.ignore, filter.triggers);
			impl->system->GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {}, layers, bodies);
			if (!collector.HadHit()) {
				return false;
			}
			const JPH::ShapeCastResult& result = collector.mHit;
			hit.distance = result.mFraction * maxDistance;
			const JPH::Vec3 axis = result.mPenetrationAxis;
			hit.normal = axis.LengthSq() > 1e-12f ? -ToGlm(axis.Normalized()) : -direction;
			CompleteHit(*impl->system, result.mBodyID2, result.mSubShapeID2, JPH::RVec3(result.mContactPointOn2), hit, false);
			return true;
		}

		void PhysicsWorld::Overlap(const QueryShape& shape, const glm::vec3& center, const glm::quat& rotation, const QueryFilter& filter,
			std::vector<RayHit>& found) const {
			found.clear();
			const JPH::RefConst<JPH::Shape> queryShape = MakeQueryShape(shape);
			const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(ToJolt(rotation), JPH::RVec3(center.x, center.y, center.z));
			JPH::CollideShapeSettings settings;
			JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
			const LayerMaskFilter layers(filter.layerMask);
			const QueryBodyFilter bodies(filter.ignore, filter.triggers);
			impl->system->GetNarrowPhaseQuery().CollideShape(queryShape, JPH::Vec3::sReplicate(1.0f), transform, settings, JPH::RVec3::sZero(),
				collector, {}, layers, bodies);
			for (const JPH::CollideShapeResult& result : collector.mHits) {
				if (std::any_of(found.begin(), found.end(), [&](const RayHit& h) { return h.body == result.mBodyID2.GetIndexAndSequenceNumber(); })) {
					continue;
				}
				RayHit hit;
				const JPH::Vec3 axis = result.mPenetrationAxis;
				hit.normal = axis.LengthSq() > 1e-12f ? -ToGlm(axis.Normalized()) : glm::vec3(0.0f, 1.0f, 0.0f);
				CompleteHit(*impl->system, result.mBodyID2, result.mSubShapeID2, JPH::RVec3(result.mContactPointOn2), hit, false);
				found.push_back(hit);
			}
		}

		void PhysicsWorld::SetLayerMatrix(const uint32_t (&masks)[LAYER_COUNT]) {
			for (int i = 0; i < LAYER_COUNT; i++) {
				impl->objectPairs.masks[i] = masks[i];
			}
		}

		void PhysicsWorld::SetLayer(uint32_t body, int layer) {
			const JPH::BodyID id(body);
			const bool moving = impl->Bodies().GetMotionType(id) != JPH::EMotionType::Static;
			impl->Bodies().SetObjectLayer(id, ObjectLayer(layer, moving));
		}

		int PhysicsWorld::GetLayer(uint32_t body) const {
			return UserLayer(impl->Bodies().GetObjectLayer(JPH::BodyID(body)));
		}

		bool PhysicsWorld::IsTrigger(uint32_t body) const {
			JPH::BodyLockRead lock(impl->system->GetBodyLockInterface(), JPH::BodyID(body));
			return lock.Succeeded() && lock.GetBody().IsSensor();
		}

		std::vector<PhysicsWorld::ContactEvent> PhysicsWorld::TakeContactEvents() {
			return impl->contacts.Take(impl->Bodies());
		}

		void PhysicsWorld::SetVelocity(uint32_t body, const glm::vec3& linear, const glm::vec3& angular) {
			impl->Bodies().SetLinearAndAngularVelocity(JPH::BodyID(body), ToJolt(linear), ToJolt(angular));
		}

		void PhysicsWorld::MoveKinematic(uint32_t body, const glm::vec3& position, const glm::quat& rotation, float stepTime) {
			impl->Bodies().MoveKinematic(JPH::BodyID(body), JPH::RVec3(position.x, position.y, position.z), ToJolt(rotation), stepTime);
		}

		bool PhysicsWorld::IsActive(uint32_t body) const {
			return impl->Bodies().IsActive(JPH::BodyID(body));
		}

		bool PhysicsWorld::ApplyBuoyancy(uint32_t body, const glm::vec3& surfacePoint, const glm::vec3& surfaceNormal, float buoyancy,
			float linearDrag, float angularDrag, const glm::vec3& fluidVelocity, float stepTime) {
			return impl->Bodies().ApplyBuoyancyImpulse(JPH::BodyID(body), JPH::RVec3(surfacePoint.x, surfacePoint.y, surfacePoint.z),
				ToJolt(surfaceNormal), buoyancy, linearDrag, angularDrag, ToJolt(fluidVelocity), impl->system->GetGravity(), stepTime);
		}

		float PhysicsWorld::SubmergedFraction(uint32_t body, const glm::vec3& surfacePoint, const glm::vec3& surfaceNormal) const {
			JPH::BodyLockRead lock(impl->system->GetBodyLockInterface(), JPH::BodyID(body));
			if (!lock.Succeeded()) {
				return 0.0f;
			}
			float total = 0.0f, submerged = 0.0f;
			JPH::Vec3 center;
			lock.GetBody().GetSubmergedVolume(JPH::RVec3(surfacePoint.x, surfacePoint.y, surfacePoint.z), ToJolt(surfaceNormal), total, submerged, center);
			return total > 0.0f ? std::clamp(submerged / total, 0.0f, 1.0f) : 0.0f;
		}

		size_t PhysicsWorld::GetBodyCount() const {
			return impl->bodies.size();
		}
	}
}
