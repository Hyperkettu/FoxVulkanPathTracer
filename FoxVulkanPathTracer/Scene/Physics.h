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
				uint64_t user = 0;
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

			void GetTransform(uint32_t body, glm::vec3& position, glm::quat& rotation) const;
			void SetTransform(uint32_t body, const glm::vec3& position, const glm::quat& rotation);
			glm::vec3 GetLinearVelocity(uint32_t body) const;
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
