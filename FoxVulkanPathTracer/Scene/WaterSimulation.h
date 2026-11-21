#pragma once

#include <memory>
#include <vector>

namespace Fox {

	namespace Graphics::Vulkan {
		class DescriptorSetBuilder;
		class DescriptorSet;
		class PipelineLayout;
		class Buffer;
	}

	namespace Scene {

		// A body of water, simulated on the GPU with the shallow water equations: the Navier-Stokes equations
		// averaged over the depth, which is what a lake, a pond or a river is to the eye (its waves are much longer
		// than it is deep, or near enough). Each step (Shaders/water.slang):
		//
		//   velocity  carried along by itself (advection), spread by viscosity, pushed down the slope of the surface
		//             by gravity (the pressure term), slowed by the bed and driven by the wind
		//   depth     the water that flows in minus what flows out: the volume is conserved, and the depth never
		//             goes below zero (the outflow of a cell is limited to what it holds)
		//
		// The water lies on a bed (the terrain or the ground plane, sampled once) and is free to flow over it: it
		// fills hollows, runs down slopes, and leaves dry land dry. The grid's edges are walls. Rain drops and
		// splashes stir it. Each frame it writes its surface into the scene's vertex buffers (the scene refits the
		// surface's acceleration structure), with where each vertex was before, for motion vectors.
		//
		// Foam: a field on the grid, made where the water breaks or churns (steep wave faces, converging flow, the
		// shore, fast water, drops and splashes, spray falling back), carried by the current and fading. Its
		// amount goes with each surface vertex; the closest hit shader turns it into white foam.
		//
		// Spray: droplets thrown up where foam is made and off wave crests in strong wind, flying under gravity
		// and air drag (drifting with the wind) and landing back as foam. Each is a tiny octahedron of water in a
		// second surface of the scene, rebuilt every frame.
		class WaterSimulation {
		public:
			struct Grid {
				uint32_t width = 0, depth = 0; // Cells
				float cellSize = 0.25f;        // Metres
				glm::vec3 origin{ 0.0f };      // World position of the corner (x, z)
			};

			// What can change while it runs
			struct Parameters {
				float viscosity = 0.001f;     // m^2/s: an eddy viscosity (the 1e-6 of water itself is far below the grid)
				float friction = 0.003f;      // Bed friction
				glm::vec2 wind{ 0.0f };       // Stress on the surface, m/s^2, along x and z
				float gust = 0.6f;            // 0..1
				float windSpeed = 0.0f;       // m/s, and where it blows to (radians): spray drifts with it
				float windAngle = 0.0f;
				float timeScale = 1.0f;
				float foamAmount = 1.0f;
				float foamLifetime = 6.0f;    // Seconds to fade to a third
				float sprayAmount = 1.0f;
				float dropletSize = 0.01f;    // Metres (radius)
			};

			// bed: ground height under each cell (world y), depth: initial water depth per cell (metres).
			// The surface's vertices are at firstVertex in the scene's vertex buffers, its previous positions at
			// firstPrevious in the previous positions buffer.
			WaterSimulation(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue,
				const Grid& grid, const std::vector<float>& bed, const std::vector<float>& depth,
				uint32_t firstVertex, uint32_t firstPrevious, VkBuffer vertices, VkBuffer buildVertices, VkBuffer previousPositions,
				uint32_t particleCount = 0, uint32_t particleFirstVertex = 0, uint32_t particleFirstPrevious = 0);
			~WaterSimulation();

			WaterSimulation(const WaterSimulation&) = delete;
			WaterSimulation& operator=(const WaterSimulation&) = delete;

			// A drop or splash at x, z (metres from the corner): a dip of `amount` metres with a ring around it
			void AddDrop(float x, float z, float radius, float amount);

			// Steps the water by deltaTime (in as many small steps as it needs to stay stable) and writes the surface.
			// frameSlot: the frame in flight (its readback is only reused once that frame has finished).
			void Record(VkCommandBuffer cmd, float deltaTime, const Parameters& parameters, uint32_t frameSlot);

			// The water as it was a few frames ago (read back every frame, for floating bodies): its surface height,
			// normal and flow velocity (x, z) at a world position. False where there is none (dry, or off the grid).
			bool SampleSurface(float worldX, float worldZ, float& height, glm::vec3& normal, glm::vec2& velocity) const;

			const Grid& GetGrid() const { return grid; }
			uint32_t LastSubsteps() const { return lastSubsteps; }
			// Spray droplets flying a few frames ago (read back), or ~0u before the first readback
			uint32_t FlyingDroplets() const { return flyingDroplets; }

		private:
			std::unique_ptr<Fox::Graphics::Vulkan::Buffer> MakeBuffer(VkDeviceSize size, const void* data);
			void Barrier(VkCommandBuffer cmd) const;

			VkDevice device = VK_NULL_HANDLE;
			VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
			VkCommandPool commandPool = VK_NULL_HANDLE;
			VkQueue queue = VK_NULL_HANDLE;
			Grid grid;
			uint32_t firstVertex = 0, firstPrevious = 0;
			uint32_t particleCount = 0, particleFirstVertex = 0, particleFirstPrevious = 0; // Spray droplets
			float maxDepth = 1.0f;
			float time = 0.0f;
			uint32_t lastSubsteps = 0;

			std::unique_ptr<Fox::Graphics::Vulkan::Buffer> bed, depth[2], u[2], v[2], outflow, drops;
			std::unique_ptr<Fox::Graphics::Vulkan::Buffer> foam[2], landed, particles, particleCounter, churn;
			std::unique_ptr<Fox::Graphics::Vulkan::DescriptorSetBuilder> descriptors;
			std::unique_ptr<Fox::Graphics::Vulkan::PipelineLayout> layout;
			std::unique_ptr<Fox::Graphics::Vulkan::DescriptorSet> sets[2]; // [current]: reads state `current`, writes the other
			VkPipeline pipelines[7] = {};
			int current = 0;

			static constexpr uint32_t MAX_DROPS = 64;
			std::vector<glm::vec4> pendingDrops;

			// Depth and velocities, read back every frame into the buffer of its frame in flight: for floating bodies,
			// and the volume (what drops and rounding took is spread back over the water; FOX_WATER_DEBUG=1 logs it)
			static constexpr uint32_t READBACK_SLOTS = 4;
			std::unique_ptr<Fox::Graphics::Vulkan::Buffer> readback[READBACK_SLOTS];
			bool readbackWritten[READBACK_SLOTS] = {};
			std::vector<float> bedHeights, cpuDepth, cpuU, cpuV; // The latest read back
			void TakeReadback(uint32_t slot, uint32_t substeps, float dt);
			uint32_t frame = 0;
			double initialVolume = 0.0;
			float correction = 0.0f; // Metres per second added to every wet cell
			uint32_t flyingDroplets = ~0u;
		};
	}
}
