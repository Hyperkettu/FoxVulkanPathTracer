// C# scripts: the engine's functions they call (the NativeApi of Scripting/ScriptApi.h), and running them while the
// game plays (ScriptComponent).

#include "FoxRenderer.h"
#include "FoxVulkanPathTracer/Core/TinyGLTF/json.hpp"

#include <algorithm>
#include <set>

namespace Fox {

	namespace Scene {

		namespace RayTracing {

			using Entity = MainRayTracingScene::Entity;
			using Fox::Scripting::ComponentType;
			using Fox::Scripting::Property;

			// The functions scripts call. Static: they are C function pointers; the scene is the one running scripts.
			struct ScriptBindings {
				static inline MainRayTracingScene* scene = nullptr;

				static uint64_t Id(Entity entity) {
					return entity.IsNull() ? 0 : (static_cast<uint64_t>(entity.generation) << 32) | (static_cast<uint64_t>(entity.index) + 1);
				}
				static Entity From(uint64_t id) {
					Entity entity;
					if (id != 0) {
						entity.index = static_cast<uint32_t>(id & 0xffffffffu) - 1;
						entity.generation = static_cast<uint32_t>(id >> 32);
					}
					return entity;
				}
				static SceneGraph& Graph() { return scene->graph; }
				// The entity, if it still exists
				static bool Valid(uint64_t id, Entity& entity) {
					entity = From(id);
					return scene && !entity.IsNull() && Graph().IsValid(entity);
				}

				static void Log(int32_t level, const char* message) {
					static const char* prefixes[] = { "", "Warning: ", "Error: " };
					std::cout << prefixes[std::clamp(level, 0, 2)] << (message ? message : "") << std::endl;
				}

				static uint64_t FindEntity(const char* name) {
					if (!name) return 0;
					for (Entity entity : Graph().DepthFirst()) {
						if (Graph().GetName(entity) == name) return Id(entity);
					}
					return 0;
				}
				static int32_t IsValid(uint64_t id) { Entity e; return Valid(id, e) ? 1 : 0; }
				static int32_t GetName(uint64_t id, char* buffer, int32_t capacity) {
					Entity e;
					if (!Valid(id, e)) return 0;
					const std::string& name = Graph().GetName(e);
					if (buffer && capacity > 0) {
						const size_t n = std::min(name.size(), static_cast<size_t>(capacity - 1));
						std::memcpy(buffer, name.data(), n);
						buffer[n] = 0;
					}
					return static_cast<int32_t>(name.size());
				}
				static void SetName(uint64_t id, const char* name) { Entity e; if (Valid(id, e) && name) Graph().SetName(e, name); }
				static int32_t GetActive(uint64_t id) { Entity e; return Valid(id, e) && Graph().IsActive(e) ? 1 : 0; }
				static void SetActive(uint64_t id, int32_t active) { Entity e; if (Valid(id, e)) Graph().SetActive(e, active != 0); }
				static uint64_t GetParent(uint64_t id) { Entity e; return Valid(id, e) ? Id(Graph().GetParent(e)) : 0; }
				static void SetParent(uint64_t id, uint64_t parent) {
					Entity e, p;
					if (Valid(id, e)) Graph().SetParent(e, Valid(parent, p) ? p : Entity{}, true);
				}
				static int32_t GetChildren(uint64_t id, uint64_t* children, int32_t capacity) {
					Entity e;
					if (!Valid(id, e)) return 0;
					const auto& list = Graph().GetChildren(e);
					for (int32_t i = 0; children && i < capacity && i < static_cast<int32_t>(list.size()); i++) children[i] = Id(list[i]);
					return static_cast<int32_t>(list.size());
				}

				static void GetTransform(uint64_t id, int32_t world, float* t) {
					Entity e;
					Transform transform;
					if (Valid(id, e)) transform = world ? Graph().GetWorldTransform(e) : Graph().GetLocalTransform(e);
					const float values[10] = { transform.position.x, transform.position.y, transform.position.z,
						transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w,
						transform.scale.x, transform.scale.y, transform.scale.z };
					std::memcpy(t, values, sizeof(values));
				}
				static void SetTransform(uint64_t id, int32_t world, const float* t) {
					Entity e;
					if (!Valid(id, e) || !Graph().HasTransform(e)) return;
					Transform transform;
					transform.position = glm::vec3(t[0], t[1], t[2]);
					transform.rotation = glm::quat(t[6], t[3], t[4], t[5]);
					if (glm::dot(transform.rotation, transform.rotation) < 1e-12f) transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
					transform.rotation = glm::normalize(transform.rotation);
					transform.scale = glm::vec3(t[7], t[8], t[9]);
					if (world) Graph().SetWorldTransform(e, transform);
					else Graph().SetLocalTransform(e, transform);
				}

				static uint64_t CreateEntity(const char* name, uint64_t parent) {
					Entity p;
					return Id(Graph().CreateEntity(name ? name : "Entity", Valid(parent, p) ? p : Entity{}));
				}
				static uint64_t Spawn(const char* what, const float* position) {
					if (!scene || !what) return 0;
					std::string kind(what);
					std::string lower = kind;
					std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					const glm::vec3 p(position[0], position[1], position[2]);
					if (lower == "ball") return Id(scene->AddPhysicsBall(p));
					if (lower == "box") return Id(scene->AddPhysicsCrate(p));
					if (lower == "sphere") return Id(scene->AddSphereObject(p, 0.5f, SurfaceMaterial{ glm::vec3(0.8f), 0.4f }));
					if (lower == "smoke") return Id(scene->AddSmoke(p));
					if (lower == "glowing smoke") return Id(scene->AddSmoke(p, true));
					if (lower == "agent") return Id(scene->AddNavAgent(p));
					if (lower == "empty") return Id(scene->CreateEmpty("Entity"));
					if (std::filesystem::exists(kind)) return Id(scene->AddModel(kind, false, p));
					std::cout << "Scripts: Spawn(\"" << kind << "\"): not a kind of thing (ball, box, sphere, smoke, glowing smoke, agent, empty) or a model file\n";
					return 0;
				}
				static uint64_t Duplicate(uint64_t id) { Entity e; return Valid(id, e) ? Id(scene->DuplicateEntity(e)) : 0; }
				static void Destroy(uint64_t id) { Entity e; if (Valid(id, e)) scene->DestroyEntity(e); }

				static int32_t HasComponent(uint64_t id, int32_t type) {
					Entity e;
					if (!Valid(id, e)) return 0;
					auto& r = Graph().GetRegistry();
					switch (static_cast<ComponentType>(type)) {
					case ComponentType::Mesh: return r.Has<MeshComponent>(e);
					case ComponentType::Light: return r.Has<LightComponent>(e);
					case ComponentType::Physics: return r.Has<PhysicsComponent>(e);
					case ComponentType::NavAgent: return r.Has<NavAgentComponent>(e);
					case ComponentType::Volume: return r.Has<VolumeComponent>(e);
					case ComponentType::Camera: return r.Has<CameraComponent>(e);
					case ComponentType::Script: return r.Has<ScriptComponent>(e);
					case ComponentType::Water: return r.Has<WaterComponent>(e);
					case ComponentType::Sound: return r.Has<SoundComponent>(e);
					}
					return 0;
				}
				static int32_t AddComponent(uint64_t id, int32_t type) {
					Entity e;
					if (!Valid(id, e) || HasComponent(id, type)) return 0;
					auto& r = Graph().GetRegistry();
					switch (static_cast<ComponentType>(type)) {
					case ComponentType::Mesh: scene->AddMeshComponent(e, MeshComponent::Type::Sphere); return 1;
					case ComponentType::Light: scene->AddLightComponent(e, LightComponent::Type::Point); return 1;
					case ComponentType::Physics: r.Add<PhysicsComponent>(e); return 1; // Its body joins next frame
					case ComponentType::NavAgent: r.Add<NavAgentComponent>(e); return 1;
					case ComponentType::Volume: r.Add<VolumeComponent>(e); return 1;
					case ComponentType::Camera: r.Add<CameraComponent>(e); return 1;
					case ComponentType::Script: r.Add<ScriptComponent>(e); return 1;
					case ComponentType::Water: r.Add<WaterComponent>(e); scene->MarkStructureChanged(); return 1;
					case ComponentType::Sound: r.Add<SoundComponent>(e).playOnStart = false; return 1;
					}
					return 0;
				}
				static void RemoveComponent(uint64_t id, int32_t type) {
					Entity e;
					if (!Valid(id, e)) return;
					auto& r = Graph().GetRegistry();
					switch (static_cast<ComponentType>(type)) {
					case ComponentType::Mesh: scene->RemoveMeshComponent(e); break;
					case ComponentType::Light: scene->RemoveLightComponent(e); break;
					case ComponentType::Physics: r.Remove<PhysicsComponent>(e); break; // Its body leaves next frame
					case ComponentType::NavAgent: r.Remove<NavAgentComponent>(e); break;
					case ComponentType::Volume: r.Remove<VolumeComponent>(e); break;
					case ComponentType::Camera: r.Remove<CameraComponent>(e); break;
					case ComponentType::Script: r.Remove<ScriptComponent>(e); break; // Its scripts stop next frame
					case ComponentType::Sound: scene->StopEntitySound(e); r.Remove<SoundComponent>(e); break;
					case ComponentType::Water:
						if (WaterComponent* water = r.TryGet<WaterComponent>(e)) water->enabled = false;
						scene->MarkStructureChanged();
						break;
					}
				}

				// The playing body of an entity (INVALID_BODY: none)
				static uint32_t Body(Entity e) {
					const PhysicsComponent* body = Graph().GetRegistry().TryGet<PhysicsComponent>(e);
					return body && scene->physics && scene->physicsPlaying ? body->body : PhysicsWorld::INVALID_BODY;
				}

				static int32_t GetProperty(uint64_t id, int32_t property, float* v) {
					v[0] = v[1] = v[2] = v[3] = 0.0f;
					Entity e;
					if (!Valid(id, e)) return 0;
					auto& r = Graph().GetRegistry();
					auto vec = [&](const glm::vec3& x) { v[0] = x.x; v[1] = x.y; v[2] = x.z; return 1; };
					switch (static_cast<Property>(property)) {
					case Property::PhysicsVelocity: { const uint32_t b = Body(e); return b != PhysicsWorld::INVALID_BODY ? vec(scene->physics->GetLinearVelocity(b)) : 0; }
					case Property::PhysicsAngularVelocity: { const uint32_t b = Body(e); return b != PhysicsWorld::INVALID_BODY ? vec(scene->physics->GetAngularVelocity(b)) : 0; }
					case Property::PhysicsMass: { const uint32_t b = Body(e); if (b == PhysicsWorld::INVALID_BODY) return 0; v[0] = scene->physics->GetBodyInfo(b).mass; return 1; }
					case Property::NavTarget: if (auto* n = r.TryGet<NavAgentComponent>(e)) return vec(n->target); return 0;
					case Property::NavSpeed: if (auto* n = r.TryGet<NavAgentComponent>(e)) { v[0] = n->speed; return 1; } return 0;
					case Property::NavArrived: if (auto* n = r.TryGet<NavAgentComponent>(e)) { v[0] = n->arrived ? 1.0f : 0.0f; return 1; } return 0;
					case Property::NavHasTarget: if (auto* n = r.TryGet<NavAgentComponent>(e)) { v[0] = n->hasTarget ? 1.0f : 0.0f; return 1; } return 0;
					case Property::LightColor: if (auto* l = r.TryGet<LightComponent>(e)) return vec(l->color); return 0;
					case Property::LightIntensity: if (auto* l = r.TryGet<LightComponent>(e)) { v[0] = l->intensity; return 1; } return 0;
					case Property::LightEnabled: if (auto* l = r.TryGet<LightComponent>(e)) { v[0] = l->enabled ? 1.0f : 0.0f; return 1; } return 0;
					case Property::MeshVisible: if (auto* m = r.TryGet<MeshComponent>(e)) { v[0] = m->enabled ? 1.0f : 0.0f; return 1; } return 0;
					case Property::CameraFov: if (auto* c = r.TryGet<CameraComponent>(e)) { v[0] = c->fov; return 1; } return 0;
					case Property::VolumeDensity: if (auto* x = r.TryGet<VolumeComponent>(e)) { v[0] = x->density; return 1; } return 0;
					case Property::VolumeColor: if (auto* x = r.TryGet<VolumeComponent>(e)) return vec(x->color); return 0;
					case Property::VolumeGlow: if (auto* x = r.TryGet<VolumeComponent>(e)) { v[0] = x->glowStrength; return 1; } return 0;
					case Property::PhysicsLayer: if (auto* p = r.TryGet<PhysicsComponent>(e)) { v[0] = static_cast<float>(p->layer); return 1; } return 0;
					case Property::PhysicsTrigger: if (auto* p = r.TryGet<PhysicsComponent>(e)) { v[0] = p->trigger ? 1.0f : 0.0f; return 1; } return 0;
					case Property::SoundVolume: if (auto* x = r.TryGet<SoundComponent>(e)) { v[0] = x->volume; return 1; } return 0;
					case Property::SoundPitch: if (auto* x = r.TryGet<SoundComponent>(e)) { v[0] = x->pitch; return 1; } return 0;
					case Property::SoundLoop: if (auto* x = r.TryGet<SoundComponent>(e)) { v[0] = x->loop ? 1.0f : 0.0f; return 1; } return 0;
					default: return 0;
					}
				}

				static int32_t SetProperty(uint64_t id, int32_t property, const float* v) {
					Entity e;
					if (!Valid(id, e)) return 0;
					auto& r = Graph().GetRegistry();
					const glm::vec3 x(v[0], v[1], v[2]);
					switch (static_cast<Property>(property)) {
					case Property::PhysicsVelocity: {
						const uint32_t b = Body(e);
						if (b == PhysicsWorld::INVALID_BODY) return 0;
						scene->physics->SetVelocity(b, x, scene->physics->GetAngularVelocity(b));
						return 1;
					}
					case Property::PhysicsAngularVelocity: {
						const uint32_t b = Body(e);
						if (b == PhysicsWorld::INVALID_BODY) return 0;
						scene->physics->SetVelocity(b, scene->physics->GetLinearVelocity(b), x);
						return 1;
					}
					case Property::NavSpeed: if (auto* n = r.TryGet<NavAgentComponent>(e)) { n->speed = std::max(v[0], 0.0f); return 1; } return 0;
					case Property::NavHasTarget:
						if (auto* n = r.TryGet<NavAgentComponent>(e)) {
							if (v[0] == 0.0f) { n->hasTarget = false; n->path.clear(); n->arrived = true; }
							return 1;
						}
						return 0;
					case Property::LightColor: if (auto* l = r.TryGet<LightComponent>(e)) { l->color = x; return 1; } return 0;
					case Property::LightIntensity: if (auto* l = r.TryGet<LightComponent>(e)) { l->intensity = v[0]; return 1; } return 0;
					case Property::LightEnabled: if (auto* l = r.TryGet<LightComponent>(e)) { l->enabled = v[0] != 0.0f; return 1; } return 0;
					case Property::MeshVisible: if (auto* m = r.TryGet<MeshComponent>(e)) { m->enabled = v[0] != 0.0f; return 1; } return 0;
					case Property::CameraFov: if (auto* c = r.TryGet<CameraComponent>(e)) { c->fov = v[0]; return 1; } return 0;
					case Property::VolumeDensity: if (auto* y = r.TryGet<VolumeComponent>(e)) { y->density = std::max(v[0], 0.0f); return 1; } return 0;
					case Property::VolumeColor: if (auto* y = r.TryGet<VolumeComponent>(e)) { y->color = x; return 1; } return 0;
					case Property::VolumeGlow: if (auto* y = r.TryGet<VolumeComponent>(e)) { y->glowStrength = std::max(v[0], 0.0f); return 1; } return 0;
					case Property::SoundVolume:
						if (auto* x = r.TryGet<SoundComponent>(e)) {
							x->volume = std::max(v[0], 0.0f);
							if (auto* engine = scene->GetAudio(); engine && x->voice) engine->SetVolume(x->voice, x->volume);
							return 1;
						}
						return 0;
					case Property::SoundPitch:
						if (auto* x = r.TryGet<SoundComponent>(e)) {
							x->pitch = std::clamp(v[0], 0.01f, 8.0f);
							if (auto* engine = scene->GetAudio(); engine && x->voice) engine->SetPitch(x->voice, x->pitch);
							return 1;
						}
						return 0;
					case Property::SoundLoop: if (auto* x = r.TryGet<SoundComponent>(e)) { x->loop = v[0] != 0.0f; return 1; } return 0;
					case Property::PhysicsLayer:
						if (auto* p = r.TryGet<PhysicsComponent>(e)) {
							p->layer = std::clamp(static_cast<int>(v[0]), 0, PhysicsWorld::LAYER_COUNT - 1);
							const uint32_t b = Body(e);
							if (b != PhysicsWorld::INVALID_BODY) scene->physics->SetLayer(b, p->layer);
							return 1;
						}
						return 0;
					default: return 0;
					}
				}

				static void AddImpulse(uint64_t id, const float* linear, const float* angular) {
					Entity e;
					if (!Valid(id, e)) return;
					const uint32_t b = Body(e);
					if (b == PhysicsWorld::INVALID_BODY) return;
					scene->physics->AddImpulse(b, linear ? glm::vec3(linear[0], linear[1], linear[2]) : glm::vec3(0.0f),
						angular ? glm::vec3(angular[0], angular[1], angular[2]) : glm::vec3(0.0f));
				}

				static int32_t AddScript(uint64_t id, const char* className) {
					Entity e;
					if (!Valid(id, e) || !className) return 0;
					auto& r = Graph().GetRegistry();
					ScriptComponent* component = r.TryGet<ScriptComponent>(e);
					if (!component) component = &r.Add<ScriptComponent>(e);
					ScriptComponent::Entry entry;
					entry.className = className;
					component->scripts.push_back(std::move(entry));
					return 1;
				}

				static void SetNavTarget(uint64_t id, const float* target) {
					Entity e;
					if (Valid(id, e)) scene->SetNavTarget(e, glm::vec3(target[0], target[1], target[2]));
				}

				static int32_t Raycast(const float* origin, const float* direction, float maxDistance, uint64_t ignore, uint64_t* entity, float* hit) {
					*entity = 0;
					const glm::vec3 o(origin[0], origin[1], origin[2]);
					glm::vec3 d(direction[0], direction[1], direction[2]);
					if (glm::dot(d, d) < 1e-12f) return 0;
					d = glm::normalize(d);
					if (scene->physics && scene->physicsPlaying) {
						Entity skip;
						const uint32_t ignored = Valid(ignore, skip) ? Body(skip) : PhysicsWorld::INVALID_BODY;
						PhysicsWorld::RayHit result;
						if (!scene->physics->CastRay(o, d, maxDistance, ignored, result)) return 0;
						for (const auto& playing : scene->playingBodies) {
							if (playing.body == result.body) {
								*entity = Id(playing.entity);
								break;
							}
						}
						const float values[7] = { result.point.x, result.point.y, result.point.z, result.normal.x, result.normal.y, result.normal.z, result.distance };
						std::memcpy(hit, values, sizeof(values));
						return 1;
					}
					// Not playing: the ground only
					glm::vec3 point;
					if (!scene->RaycastGround(o, d, point) || glm::distance(o, point) > maxDistance) return 0;
					const float values[7] = { point.x, point.y, point.z, 0.0f, 1.0f, 0.0f, glm::distance(o, point) };
					std::memcpy(hit, values, sizeof(values));
					return 1;
				}

				static int32_t Edge(uint8_t now, uint8_t before, int32_t mode) {
					return mode == 0 ? now : mode == 1 ? (now && !before) : (!now && before);
				}
				static int32_t GetKey(int32_t scancode, int32_t mode) {
					if (scancode < 0 || scancode >= 512) return 0;
					const auto& input = scene->scriptInput;
					return Edge(input.keys[scancode], input.previousKeys[scancode], mode);
				}
				static int32_t GetMouseButton(int32_t button, int32_t mode) {
					if (button < 0 || button >= 3) return 0;
					const auto& input = scene->scriptInput;
					return Edge(input.buttons[button], input.previousButtons[button], mode);
				}
				static void GetMouse(float* v) {
					const auto& input = scene->scriptInput;
					v[0] = input.mouseDelta.x; v[1] = input.mouseDelta.y; v[2] = input.mousePosition.x; v[3] = input.mousePosition.y;
				}
				static void GetTime(double* v) {
					const auto& clock = scene->clock;
					v[0] = clock.time; v[1] = clock.delta; v[2] = clock.timeScale; v[3] = static_cast<double>(clock.frame);
				}
				static uint64_t GetGameCamera() { return Id(scene->FindGameCamera()); }

				// --- Queries ---

				static bool Playing() { return scene->physics && scene->physicsPlaying; }

				// The entity of a playing body (null for the ground)
				static Entity EntityOfBody(uint32_t body) {
					for (const auto& playing : scene->playingBodies) {
						if (playing.body == body) return playing.entity;
					}
					return Entity{};
				}

				static PhysicsWorld::QueryFilter Filter(uint32_t layerMask, uint64_t ignore, int32_t triggers) {
					PhysicsWorld::QueryFilter filter;
					filter.layerMask = layerMask;
					filter.triggers = triggers != 0;
					Entity skip;
					if (Valid(ignore, skip)) filter.ignore = Body(skip);
					return filter;
				}

				static void WriteHit(const PhysicsWorld::RayHit& hit, uint64_t* entity, float* out) {
					*entity = Id(EntityOfBody(hit.body));
					const float values[7] = { hit.point.x, hit.point.y, hit.point.z, hit.normal.x, hit.normal.y, hit.normal.z, hit.distance };
					std::memcpy(out, values, sizeof(values));
				}

				static PhysicsWorld::QueryShape Shape(int32_t type, const float* size) {
					PhysicsWorld::QueryShape shape;
					switch (static_cast<Fox::Scripting::QueryShapeType>(type)) {
					case Fox::Scripting::QueryShapeType::Box:
						shape.type = PhysicsWorld::QueryShape::Type::Box;
						shape.halfExtents = glm::vec3(size[0], size[1], size[2]);
						break;
					case Fox::Scripting::QueryShapeType::Capsule:
						shape.type = PhysicsWorld::QueryShape::Type::Capsule;
						shape.radius = size[0];
						shape.halfHeight = size[1];
						break;
					default:
						shape.type = PhysicsWorld::QueryShape::Type::Sphere;
						shape.radius = size[0];
						break;
					}
					return shape;
				}

				static glm::quat Rotation(const float* q) {
					glm::quat rotation(q[3], q[0], q[1], q[2]);
					return glm::dot(rotation, rotation) > 1e-12f ? glm::normalize(rotation) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
				}

				static int32_t RaycastAll(const float* origin, const float* direction, float maxDistance, uint32_t layerMask, uint64_t ignore,
					int32_t triggers, uint64_t* entities, float* hits, int32_t capacity) {
					const glm::vec3 o(origin[0], origin[1], origin[2]);
					glm::vec3 d(direction[0], direction[1], direction[2]);
					if (glm::dot(d, d) < 1e-12f || capacity <= 0) return 0;
					d = glm::normalize(d);
					std::vector<PhysicsWorld::RayHit> found;
					if (Playing()) {
						const auto filter = Filter(layerMask, ignore, triggers);
						if (capacity == 1) {
							PhysicsWorld::RayHit hit;
							if (scene->physics->CastRay(o, d, maxDistance, filter, hit)) found.push_back(hit);
						}
						else {
							scene->physics->CastRayAll(o, d, maxDistance, filter, found);
						}
					}
					else if ((layerMask >> PhysicsWorld::GROUND_LAYER) & 1u) {
						// Not playing: the ground only
						glm::vec3 point;
						if (scene->RaycastGround(o, d, point) && glm::distance(o, point) <= maxDistance) {
							PhysicsWorld::RayHit hit;
							hit.point = point;
							hit.distance = glm::distance(o, point);
							found.push_back(hit);
						}
					}
					const int32_t count = static_cast<int32_t>(found.size());
					for (int32_t i = 0; i < std::min(count, capacity); i++) WriteHit(found[i], entities + i, hits + 7 * i);
					return count;
				}

				static int32_t ShapeCast(int32_t shape, const float* size, const float* origin, const float* rotation, const float* direction,
					float maxDistance, uint32_t layerMask, uint64_t ignore, int32_t triggers, uint64_t* entity, float* hit) {
					*entity = 0;
					glm::vec3 d(direction[0], direction[1], direction[2]);
					if (!Playing() || glm::dot(d, d) < 1e-12f) return 0;
					PhysicsWorld::RayHit result;
					if (!scene->physics->CastShape(Shape(shape, size), glm::vec3(origin[0], origin[1], origin[2]), Rotation(rotation), glm::normalize(d),
						maxDistance, Filter(layerMask, ignore, triggers), result)) {
						return 0;
					}
					WriteHit(result, entity, hit);
					return 1;
				}

				static int32_t Overlap(int32_t shape, const float* size, const float* center, const float* rotation, uint32_t layerMask,
					uint64_t ignore, int32_t triggers, uint64_t* entities, int32_t capacity) {
					if (!Playing()) return 0;
					std::vector<PhysicsWorld::RayHit> found;
					scene->physics->Overlap(Shape(shape, size), glm::vec3(center[0], center[1], center[2]), Rotation(rotation),
						Filter(layerMask, ignore, triggers), found);
					int32_t count = 0;
					for (const auto& hit : found) {
						const Entity e = EntityOfBody(hit.body);
						if (e.IsNull()) continue; // The ground
						if (count < capacity) entities[count] = Id(e);
						count++;
					}
					return count;
				}

				static int32_t GetLayerName(int32_t layer, char* buffer, int32_t capacity) {
					const std::string name = scene->environment.layers.Name(layer);
					if (buffer && capacity > 0) {
						const size_t n = std::min(name.size(), static_cast<size_t>(capacity - 1));
						std::memcpy(buffer, name.data(), n);
						buffer[n] = 0;
					}
					return static_cast<int32_t>(name.size());
				}

				// --- Input actions ---

				// Each unknown name once, so a typo is noticed without flooding the console
				static void Unknown(const char* kind, const char* name) {
					static std::set<std::string> told;
					if (told.insert(std::string(kind) + name).second) {
						std::cout << "Scripts: no input " << kind << " \"" << name << "\" (Window > Input Actions)\n";
					}
				}

				static int32_t GetAction(const char* name, int32_t mode) {
					if (!name) return 0;
					const auto* action = scene->inputActions.FindAction(name);
					if (!action) {
						Unknown("action", name);
						return 0;
					}
					const auto& input = scene->scriptInput;
					const bool now = Fox::Input::InputActions::AnyHeld(action->bindings, input.keys, input.buttons);
					const bool before = Fox::Input::InputActions::AnyHeld(action->bindings, input.previousKeys, input.previousButtons);
					return Edge(now ? 1 : 0, before ? 1 : 0, mode);
				}

				static float GetAxis(const char* name) {
					if (!name) return 0.0f;
					const auto* axis = scene->inputActions.FindAxis(name);
					if (!axis) {
						Unknown("axis", name);
						return 0.0f;
					}
					const auto& input = scene->scriptInput;
					const float positive = Fox::Input::InputActions::AnyHeld(axis->positive, input.keys, input.buttons) ? 1.0f : 0.0f;
					const float negative = Fox::Input::InputActions::AnyHeld(axis->negative, input.keys, input.buttons) ? 1.0f : 0.0f;
					return positive - negative;
				}

				// --- Sound ---

				static int64_t PlaySound(const char* file, int32_t spatial, const float* position, float volume, float pitch, int32_t loop,
					float minDistance, float maxDistance) {
					auto* engine = scene->GetAudio();
					if (!engine || !file) return 0;
					Fox::Audio::AudioEngine::PlayOptions options;
					options.volume = volume;
					options.pitch = pitch;
					options.loop = loop != 0;
					options.spatial = spatial != 0 && position;
					if (position) options.position = { position[0], position[1], position[2] };
					options.minDistance = minDistance;
					options.maxDistance = maxDistance;
					return engine->Play(file, options);
				}
				static void StopSound(int64_t sound, float fade) { if (auto* engine = scene->GetAudio()) engine->Stop(sound, fade); }
				static int32_t SoundIsPlaying(int64_t sound) { auto* engine = scene->GetAudio(); return engine && engine->IsPlaying(sound) ? 1 : 0; }
				static void SetSoundParameter(int64_t sound, int32_t parameter, const float* v) {
					auto* engine = scene->GetAudio();
					if (!engine) return;
					switch (parameter) {
					case 0: engine->SetVolume(sound, v[0]); break;
					case 1: engine->SetPitch(sound, v[0]); break;
					default: engine->SetPosition(sound, { v[0], v[1], v[2] }); break;
					}
				}
				static int64_t PlayMusic(const char* file, int32_t loop, float volume, float fade) {
					auto* engine = scene->GetAudio();
					return engine && file ? engine->PlayMusic(file, loop != 0, volume, fade) : 0;
				}
				static void StopMusic(float fade) { if (auto* engine = scene->GetAudio()) engine->StopMusic(fade); }
				static float GetBusVolume(int32_t bus) {
					auto* engine = scene->GetAudio();
					return engine ? engine->GetBusVolume(static_cast<Fox::Audio::AudioEngine::Bus>(std::clamp(bus, 0, 2))) : 0.0f;
				}
				static void SetBusVolume(int32_t bus, float volume) {
					if (auto* engine = scene->GetAudio()) engine->SetBusVolume(static_cast<Fox::Audio::AudioEngine::Bus>(std::clamp(bus, 0, 2)), volume);
				}
				static int32_t EntitySound(uint64_t id, int32_t action) {
					Entity e;
					if (!Valid(id, e)) return 0;
					switch (action) {
					case 0: return scene->PlayEntitySound(e) ? 1 : 0;
					case 1: scene->StopEntitySound(e); return 1;
					default: return scene->IsEntitySoundPlaying(e) ? 1 : 0;
					}
				}
				static void SetSoundFile(uint64_t id, const char* file) {
					Entity e;
					if (!Valid(id, e) || !file) return;
					if (auto* sound = Graph().GetRegistry().TryGet<SoundComponent>(e)) sound->file = file;
				}

				static void SetText(uint64_t id, const char* value) {
					Entity e;
					if (!Valid(id, e) || !value) return;
					if (auto* text = Graph().GetRegistry().TryGet<Fox::Scene::TextComponent>(e)) text->text = value;
				}

				static int32_t LayerFromName(const char* name) {
					if (!name) return -1;
					for (int i = 0; i < PhysicsWorld::LAYER_COUNT; i++) {
						if (scene->environment.layers.Name(i) == name) return i;
					}
					return -1;
				}
			};

			namespace {
				// The Inspector's field values as a JSON object, for the scripting host
				std::string FieldsJson(const std::map<std::string, std::string>& fields) {
					nlohmann::json j = nlohmann::json::object();
					for (const auto& [name, value] : fields) {
						j[name] = value;
					}
					return j.dump();
				}
			}

			void MainRayTracingScene::InitializeScripts() {
				ScriptBindings::scene = this;
				Fox::Scripting::NativeApi api{};
				api.Log = ScriptBindings::Log;
				api.FindEntity = ScriptBindings::FindEntity;
				api.IsValid = ScriptBindings::IsValid;
				api.GetName = ScriptBindings::GetName;
				api.SetName = ScriptBindings::SetName;
				api.GetActive = ScriptBindings::GetActive;
				api.SetActive = ScriptBindings::SetActive;
				api.GetParent = ScriptBindings::GetParent;
				api.SetParent = ScriptBindings::SetParent;
				api.GetChildren = ScriptBindings::GetChildren;
				api.GetTransform = ScriptBindings::GetTransform;
				api.SetTransform = ScriptBindings::SetTransform;
				api.CreateEntity = ScriptBindings::CreateEntity;
				api.Spawn = ScriptBindings::Spawn;
				api.Duplicate = ScriptBindings::Duplicate;
				api.Destroy = ScriptBindings::Destroy;
				api.HasComponent = ScriptBindings::HasComponent;
				api.AddComponent = ScriptBindings::AddComponent;
				api.RemoveComponent = ScriptBindings::RemoveComponent;
				api.GetProperty = ScriptBindings::GetProperty;
				api.SetProperty = ScriptBindings::SetProperty;
				api.AddImpulse = ScriptBindings::AddImpulse;
				api.AddScript = ScriptBindings::AddScript;
				api.SetNavTarget = ScriptBindings::SetNavTarget;
				api.Raycast = ScriptBindings::Raycast;
				api.GetKey = ScriptBindings::GetKey;
				api.GetMouseButton = ScriptBindings::GetMouseButton;
				api.GetMouse = ScriptBindings::GetMouse;
				api.GetTime = ScriptBindings::GetTime;
				api.GetGameCamera = ScriptBindings::GetGameCamera;
				api.RaycastAll = ScriptBindings::RaycastAll;
				api.ShapeCast = ScriptBindings::ShapeCast;
				api.Overlap = ScriptBindings::Overlap;
				api.GetLayerName = ScriptBindings::GetLayerName;
				api.LayerFromName = ScriptBindings::LayerFromName;
				api.GetAction = ScriptBindings::GetAction;
				api.GetAxis = ScriptBindings::GetAxis;
				api.PlaySound = ScriptBindings::PlaySound;
				api.StopSound = ScriptBindings::StopSound;
				api.SoundIsPlaying = ScriptBindings::SoundIsPlaying;
				api.SetSoundParameter = ScriptBindings::SetSoundParameter;
				api.PlayMusic = ScriptBindings::PlayMusic;
				api.StopMusic = ScriptBindings::StopMusic;
				api.GetBusVolume = ScriptBindings::GetBusVolume;
				api.SetBusVolume = ScriptBindings::SetBusVolume;
				api.EntitySound = ScriptBindings::EntitySound;
				api.SetSoundFile = ScriptBindings::SetSoundFile;
				api.SetText = ScriptBindings::SetText;

				// The game's named input
				std::string inputError;
				if (!inputActions.Load(Fox::Input::InputActions::FILE, inputError)) {
					std::cout << "Input: " << inputError << " (using the defaults)\n";
					inputActions = Fox::Input::InputActions::Defaults();
				}

				scripts = std::make_unique<Fox::Scripting::ScriptHost>();
				if (!scripts->Initialize("Scripts", api)) {
					scriptsError = scripts->GetError();
					std::cout << "Scripts: off (" << scriptsError << ")\n";
				}
			}

			void MainRayTracingScene::UpdateScripts(float step) {
				Fox::Scripting::ScriptHost* host = GetScripts();
				if (!host) {
					return;
				}
				auto& registry = graph.GetRegistry();

				// Scripts that stopped belonging: their entity or script gone, or switched off
				for (auto it = runningScripts.begin(); it != runningScripts.end();) {
					const auto [instance, entity] = *it;
					bool keep = false;
					if (graph.IsValid(entity)) {
						if (const ScriptComponent* component = registry.TryGet<ScriptComponent>(entity); component && component->enabled) {
							keep = std::any_of(component->scripts.begin(), component->scripts.end(),
								[instance](const ScriptComponent::Entry& entry) { return entry.instance == instance; });
						}
					}
					if (!keep) {
						host->DestroyInstance(instance);
						it = runningScripts.erase(it);
					}
					else {
						++it;
					}
				}

				// Scripts not running yet (play just started, or added since): they start before their first update
				registry.Each<ScriptComponent>([&](Entity entity, ScriptComponent& component) {
					if (!component.enabled) {
						return;
					}
					for (auto& entry : component.scripts) {
						if (entry.instance == 0 && !entry.className.empty()) {
							entry.instance = host->CreateInstance(ScriptBindings::Id(entity), entry.className, FieldsJson(entry.fields));
							if (entry.instance == 0) {
								entry.instance = -1; // Not tried again until play starts again (or the scripts reload)
							}
							else {
								runningScripts[entry.instance] = entity;
							}
						}
					}
				});

				if (step > 0.0f) {
					host->Update(step);
				}
			}

			void MainRayTracingScene::DispatchContacts(const std::vector<Fox::Scene::PhysicsWorld::ContactEvent>& events) {
				Fox::Scripting::ScriptHost* host = GetScripts();
				if (!host || events.empty()) {
					return;
				}
				auto hasScripts = [&](Entity entity) {
					const ScriptComponent* component = graph.IsValid(entity) ? graph.GetRegistry().TryGet<ScriptComponent>(entity) : nullptr;
					return component && component->enabled && !component->scripts.empty();
				};
				for (const auto& event : events) {
					const Entity a = ScriptBindings::EntityOfBody(event.bodyA);
					const Entity b = ScriptBindings::EntityOfBody(event.bodyB);
					using Kind = Fox::Scripting::ContactKind;
					const Kind kind = event.trigger ? (event.begin ? Kind::TriggerEnter : Kind::TriggerExit)
						: (event.begin ? Kind::CollisionEnter : Kind::CollisionExit);
					// Each side: the normal points away from the other (event.normal runs from A to B)
					auto send = [&](Entity self, Entity other, const glm::vec3& normal) {
						if (!hasScripts(self)) return;
						const float data[7] = { event.point.x, event.point.y, event.point.z, normal.x, normal.y, normal.z, event.speed };
						host->Contact(ScriptBindings::Id(self), static_cast<int32_t>(kind), ScriptBindings::Id(other), data);
					};
					send(a, b, -event.normal);
					send(b, a, event.normal);
				}
			}

			void MainRayTracingScene::StopScripts() {
				if (Fox::Scripting::ScriptHost* host = GetScripts()) {
					host->StopAll();
				}
				runningScripts.clear();
				graph.GetRegistry().Each<ScriptComponent>([](Entity, ScriptComponent& component) {
					for (auto& entry : component.scripts) {
						entry.instance = 0;
					}
				});
			}
		}
	}
}
