#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "FoxVulkanPathTracer/Graphics/Vulkan/Vertex.h"

namespace Fox {

	namespace Scene {

		// A piece of cloth simulated on the CPU with XPBD (extended position based dynamics) in small steps: a grid of
		// particles joined by the edges of its triangles (stretch and shear) and across each pair of triangles
		// (bending), pinned where its pins are, blown by the wind (drag and lift per triangle), colliding with the
		// ground, with spheres, capsules and boxes, and with itself.
		//
		// Tearing: an edge stretched past the tear limit splits the cloth at one of its ends, across the edge (a crack
		// runs across the pull, as in woven cloth): the vertex is doubled and the triangles on the far side of the
		// crack take the copy. The render mesh has its own three vertices per triangle, so splitting changes only
		// which particle each reads: the vertex and index buffers keep their size. A cut removes triangles.
		//
		// Coordinates: the rest shape is a flat sheet in its own 2D space, u across (0..width) and v down (0..height);
		// Build and Step take the world matrix (without scale) that places it.
		class ClothSimulation {
		public:
			enum class Pins { None, TopEdge, TopCorners, LeftEdge, Corners };

			struct Settings {
				float width = 2.0f, height = 2.0f; // Metres
				int resolution = 40;               // Segments across the width (down: as many as keep them square)
				bool flat = false;                 // Lying in the x-z plane (else hanging in x-y, down from the origin)
				float density = 0.25f;             // kg/m^2 (cotton ~0.15, canvas ~0.4)
				float stretchCompliance = 0.0f;    // m/N: 0 as stiff as the solver gets
				float bendCompliance = 0.5f;       // m/N of the bending springs: higher drapes softer
				float damping = 0.3f;              // Velocity lost per second (fraction)
				float thickness = 0.012f;          // Metres kept from what it collides with
				float friction = 0.5f;             // 0..1
				bool selfCollision = true;
				bool tearable = false;
				float tearStrain = 1.6f;           // An edge breaks stretched to this many times its length
				int substeps = 16;
				float drag = 1.0f;                 // Aerodynamic: the air pushing on its face
				float lift = 0.3f;                 // Aerodynamic: along the face (flutter)
				Pins pins = Pins::TopEdge;
			};

			// Something it collides with, in the world. Its previous position: where it was the last frame (it is
			// moved between the two over the substeps, so a fast body pushes the cloth along its way).
			struct Collider {
				enum class Type { Sphere, Capsule, Box } type = Type::Sphere;
				glm::vec3 position{ 0.0f }, previousPosition{ 0.0f };
				glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
				glm::vec3 halfExtents{ 0.5f }; // Box
				float radius = 0.5f;           // Sphere, capsule
				float halfHeight = 0.5f;       // Capsule: half its straight part, along its y
				// Out: the cloth's push on it this step (kg m/s at position, kg m^2/s), for two way coupling
				glm::vec3 impulse{ 0.0f }, angularImpulse{ 0.0f };
				uint64_t user = 0;
			};

			struct World {
				glm::vec3 gravity{ 0.0f, -9.81f, 0.0f };
				std::function<glm::vec3(const glm::vec3&)> wind;     // Air's velocity at a point (m/s); null: still
				std::function<float(float, float)> ground;           // Ground height at (x, z); null: none
			};

			struct Stats {
				uint32_t particles = 0, triangles = 0, torn = 0, cut = 0, constraints = 0;
				float milliseconds = 0.0f;
			};

			// A new sheet in its rest shape where world puts it
			void Build(const Settings& settings, const glm::mat4& world);
			bool IsBuilt() const { return !positions.empty(); }

			// Advances by dt (clamped to 1/30 s). Pins follow world; settings may change between steps (not its size or
			// resolution: those need Build).
			void Step(float dt, const Settings& settings, const glm::mat4& world, const World& environment, std::vector<Collider>& colliders);

			// The render mesh: three vertices per triangle (a torn away or cut triangle: NaN, no triangle), its uv
			// across the sheet (0..1); previous: each vertex's position the frame before (motion vectors)
			uint32_t RenderVertexCount() const { return static_cast<uint32_t>(triangles.size()); }
			void WriteVertices(std::vector<Fox::Graphics::Vulkan::Vertex>& vertices, std::vector<glm::vec4>& previous) const;

			// The mouse: where a ray meets the cloth (distance along it)
			bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float& distance) const;
			// Takes hold of the cloth where the ray meets it (the particles within radius of there); Drag moves the hold
			// along a new ray (at the same distance); a hard pull tears a tearable cloth.
			bool Grab(const glm::vec3& origin, const glm::vec3& direction, float radius);
			void Drag(const glm::vec3& origin, const glm::vec3& direction);
			void Release() { grabbed.clear(); }
			bool IsGrabbed() const { return !grabbed.empty(); }
			// Cuts away the triangles within radius of where the ray meets it; true when it met it
			bool Cut(const glm::vec3& origin, const glm::vec3& direction, float radius);

			const Stats& GetStats() const { return stats; }
			// The world box around it (for picking and culling colliders)
			void GetBounds(glm::vec3& low, glm::vec3& high) const { low = boundsLow; high = boundsHigh; }

		private:
			struct Constraint {
				uint32_t a, b;
				float rest;
			};
			// Tearing: how long it has been since it was made whole (it settles first), and the edges that were over the
			// limit last frame (an edge breaks over it two frames running: a real pull, not a passing jolt)
			float age = 0.0f;
			float carry = 0.0f; // Time not simulated yet (less than a substep)
			std::vector<uint64_t> strained;
			struct Grabbed {
				uint32_t particle;
				glm::vec3 offset;
			};

			Settings built;
			float spacing = 0.05f;
			uint32_t columns = 0, rows = 0;  // Particles across and down
			uint32_t maxParticles = 0;       // Room for split copies

			// Per particle
			std::vector<glm::vec3> positions, previous, velocities, framePositions, windAcceleration;
			std::vector<glm::vec2> rest;     // Where it is in the flat sheet
			std::vector<float> mass, inverseMass; // inverseMass: 0 pinned or inactive
			std::vector<float> groundHeight; // Under it, this frame
			std::vector<uint8_t> pinned, active;
			std::vector<std::vector<uint32_t>> particleTriangles;

			// Per triangle (fixed count: torn ones die)
			std::vector<glm::uvec3> triangles;
			std::vector<uint8_t> alive;

			std::vector<Constraint> stretch, bend;
			// Long range attachments: each particle no farther from its nearest pin (through the cloth, as it is torn)
			// than it is in the sheet, so a hanging cloth does not sag and stretch however fine it is
			std::vector<uint32_t> tetherAnchor;
			std::vector<float> tetherLength;
			void UpdateTethers();
			void SolveTethers();
			bool topologyChanged = false;

			// Self collision: a spatial hash
			std::vector<uint32_t> cellStart, cellEntries;
			std::vector<glm::ivec3> cells;
			std::vector<std::pair<uint32_t, uint32_t>> selfPairs;
			std::vector<glm::vec3> airVelocity, windNormals;
			void FindSelfPairs();

			std::vector<Grabbed> grabbed;
			glm::vec3 grabTarget{ 0.0f };
			float grabDistance = 0.0f;
			Pins placedPins = Pins::None;
			glm::mat4 lastWorld{ 1.0f };
			float spacingDown = 0.05f;

			Stats stats;
			glm::vec3 boundsLow{ 0.0f }, boundsHigh{ 0.0f };

			glm::vec3 LocalOf(const glm::vec2& r) const;
			bool PinnedAt(const glm::vec2& r, Pins pins) const;
			void UpdatePins(Pins pins);
			void RebuildConstraints();
			bool Split(uint32_t v, uint32_t other);
			void KillTriangle(uint32_t t);
			void Tear();
			void SolveDistance(const std::vector<Constraint>& constraints, float compliance, float h, bool backwards);
			void Collide(const World& environment, std::vector<Collider>& colliders, float substepFraction, float h);
			void CollideSelf();
			void ComputeWind(const World& environment, float dt);
			bool HitTriangle(const glm::vec3& origin, const glm::vec3& direction, float& distance, uint32_t& triangle, glm::vec3& weights) const;
		};
	}
}
