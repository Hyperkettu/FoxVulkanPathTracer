#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Fox {

	namespace Scene {

		// Rigid body physics with Jolt Physics (ThirdParty/JoltPhysics). Jolt's headers stay in Physics.cpp: the rest of
		// the renderer sees bodies as numbers and transforms as glm types.
		//
		// The world holds static ground (a height field for the terrain, or a plane) and bodies. Step advances it in
		// fixed steps of 1/60 s (a frame takes as many as its time needs, at most a few), running a callback before
		// each, where forces such as buoyancy go. Shapes built from many points (convex hulls, triangle meshes) are
		// kept by their key across Reset, so playing again does not rebuild them.
		class PhysicsWorld {
		public:
			static constexpr uint32_t INVALID_BODY = 0xffffffffu;
			static constexpr float STEP = 1.0f / 60.0f;
			static constexpr float GRAVITY = 9.81f;

			// Collision layers: every body is on one of LAYER_COUNT layers, and a matrix (SetLayerMatrix) says which
			// layers collide with which (masks of bits, one per layer). Static bodies never collide with each other.
			// Queries hit the layers in their mask. The ground is on GROUND_LAYER.
			static constexpr int LAYER_COUNT = 16;
			static constexpr int GROUND_LAYER = 1;
			static constexpr uint32_t ALL_LAYERS = 0xffffu;

			enum class Motion { Static, Dynamic, Kinematic };
			enum class ShapeType { Box, Sphere, Capsule, ConvexHull, Mesh };

			struct BodyDesc {
				Motion motion = Motion::Dynamic;
				ShapeType shape = ShapeType::Box;
				glm::vec3 halfExtents{ 0.5f }; // Box
				float radius = 0.5f;           // Sphere, capsule
				float halfHeight = 0.5f;       // Capsule: half its cylinder, along y
				glm::vec3 center{ 0.0f };      // The shape's centre in the body's space
				// ConvexHull / Mesh: in the body's space (scaled already); indices: triangles (Mesh)
				std::vector<glm::vec3> points;
				std::vector<uint32_t> indices;
				std::string cacheKey;          // Non-empty: the hull or mesh is kept under this key

				glm::vec3 position{ 0.0f };
				glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
				float mass = 0.0f;             // kg; 0: from the density
				float density = 500.0f;        // kg/m^3
				float friction = 0.5f;
				float restitution = 0.2f;
				float linearDamping = 0.05f;
				float angularDamping = 0.05f;
				bool continuous = false;       // Swept collision (fast, small bodies do not pass through others)
				int layer = 0;                 // 0..LAYER_COUNT-1
				bool trigger = false;          // A sensor: reports what enters and leaves it, collides with nothing
				uint64_t user = 0;
			};

			// Something touched or stopped touching (a trigger: entered or left), between two bodies. Collected while
			// stepping (TakeContactEvents). A pair touching in several places (a mesh's triangles) is one contact.
			struct ContactEvent {
				bool begin = true;     // false: they parted
				bool trigger = false;  // One of them is a trigger
				uint32_t bodyA = INVALID_BODY, bodyB = INVALID_BODY;
				glm::vec3 point{ 0.0f };  // Where (begin only)
				glm::vec3 normal{ 0.0f, 1.0f, 0.0f }; // From A towards B (begin only)
				float speed = 0.0f;       // How fast they met along the normal, m/s (begin only)
			};

			// What a query hits: layers in the mask, not ignore, triggers only when asked
			struct QueryFilter {
				uint32_t layerMask = ALL_LAYERS;
				uint32_t ignore = INVALID_BODY;
				bool triggers = false;
			};
			// The shape a shape cast or overlap uses (capsule: along its y)
			struct QueryShape {
				enum class Type { Sphere, Box, Capsule } type = Type::Sphere;
				float radius = 0.5f;
				glm::vec3 halfExtents{ 0.5f };
				float halfHeight = 0.5f;
			};

			struct BodyInfo {
				float mass = 0.0f;   // kg (0: static or kinematic)
				float volume = 0.0f; // m^3
				float extent = 0.0f; // Half the widest horizontal size of its bounds, metres
			};

			PhysicsWorld();
			~PhysicsWorld();
			PhysicsWorld(const PhysicsWorld&) = delete;
			PhysicsWorld& operator=(const PhysicsWorld&) = delete;

			// Removes every body and the ground (cached shapes stay)
			void Reset();
			// Forgets the cached hulls and meshes (their models changed)
			void ClearShapeCache();

			// The ground: a plane at this height, or a height field of samples x samples heights (world y, rows along
			// z) starting at corner (x, z), spacing metres apart
			void AddGroundPlane(float height);
			void AddHeightField(const std::vector<float>& heights, uint32_t samples, const glm::vec2& corner, float spacing);

			// A new body; INVALID_BODY when its shape could not be made (error says why)
			uint32_t AddBody(const BodyDesc& desc, std::string& error);
			void RemoveBody(uint32_t body);
			BodyInfo GetBodyInfo(uint32_t body) const;

			// Settles the broad phase after many bodies were added (call once before the first step)
			void Optimize();

			// Advances by deltaTime in fixed steps; beforeStep runs before each, with the step's length. Returns the
			// number of steps taken (time left over is kept for the next call).
			int Step(float deltaTime, const std::function<void(float)>& beforeStep);
			// How far time is into the next step (0 .. 1): where to draw a body between its last two steps' states
			float Interpolation() const;

			void GetTransform(uint32_t body, glm::vec3& position, glm::quat& rotation) const;
			void SetTransform(uint32_t body, const glm::vec3& position, const glm::quat& rotation);
			glm::vec3 GetLinearVelocity(uint32_t body) const;
			glm::vec3 GetAngularVelocity(uint32_t body) const;
			// A push at its centre (kg m/s) and a twist (kg m^2/s); wakes it
			void AddImpulse(uint32_t body, const glm::vec3& linear, const glm::vec3& angular);

			// Queries. Rays and casts: from origin along direction (normalised) up to maxDistance.
			struct RayHit {
				uint32_t body = INVALID_BODY;
				uint64_t user = 0;      // BodyDesc::user (0 for the ground)
				glm::vec3 point{ 0.0f };
				glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
				float distance = 0.0f;
			};
			bool CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, uint32_t ignore, RayHit& hit) const;
			// The nearest hit (all: every body it passes, nearest first)
			bool CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const QueryFilter& filter, RayHit& hit) const;
			void CastRayAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const QueryFilter& filter, std::vector<RayHit>& hits) const;
			// The shape swept from origin (turned by rotation): the first body it touches (point and normal where)
			bool CastShape(const QueryShape& shape, const glm::vec3& origin, const glm::quat& rotation, const glm::vec3& direction, float maxDistance,
				const QueryFilter& filter, RayHit& hit) const;
			// The bodies the shape at center overlaps (each once)
			void Overlap(const QueryShape& shape, const glm::vec3& center, const glm::quat& rotation, const QueryFilter& filter, std::vector<RayHit>& bodies) const;

			// Which layers collide with which: masks[i] has bit j when layer i collides with layer j (kept symmetric)
			void SetLayerMatrix(const uint32_t (&masks)[LAYER_COUNT]);
			// Moves a body to another layer while it plays
			void SetLayer(uint32_t body, int layer);
			int GetLayer(uint32_t body) const;
			bool IsTrigger(uint32_t body) const;

			// The contacts that began and ended since the last call
			std::vector<ContactEvent> TakeContactEvents();
			void SetVelocity(uint32_t body, const glm::vec3& linear, const glm::vec3& angular);
			// Kinematic: moves it to this transform over the next step (pushing what is in the way)
			void MoveKinematic(uint32_t body, const glm::vec3& position, const glm::quat& rotation, float stepTime);
			bool IsActive(uint32_t body) const;

			// Water: buoyancy and drag on the part of the body under the plane through surfacePoint with this normal.
			// buoyancy: 1 floats neutrally, more floats higher, less sinks (the fluid's density over the body's).
			// False if it is not in the water.
			bool ApplyBuoyancy(uint32_t body, const glm::vec3& surfacePoint, const glm::vec3& surfaceNormal, float buoyancy,
				float linearDrag, float angularDrag, const glm::vec3& fluidVelocity, float stepTime);
			// How much of it is under that plane: 0..1
			float SubmergedFraction(uint32_t body, const glm::vec3& surfacePoint, const glm::vec3& surfaceNormal) const;

			size_t GetBodyCount() const;

		private:
			struct Impl;
			std::unique_ptr<Impl> impl;
		};
	}
}
