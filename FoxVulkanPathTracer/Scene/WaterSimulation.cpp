#include "FoxRenderer.h"

namespace Fox {

	namespace Scene {

		namespace {
			const uint32_t GROUP_SIZE = 64; // numthreads in water.slang
			const uint32_t BINDINGS = 18;
			const int PASSES = 7;
			const char* SHADERS[PASSES] = { "Shaders/water_velocity.spv", "Shaders/water_outflow.spv", "Shaders/water_depth.spv",
				"Shaders/water_surface.spv", "Shaders/water_foam.spv", "Shaders/water_emit.spv", "Shaders/water_particles.spv" };
			const float GRAVITY = 9.81f;

			// Must match Constants in Shaders/water.slang
			struct Constants {
				uint32_t width, depth, firstVertex, firstPrevious;
				float cellSize, dt, gravity, viscosity;
				float friction, windX, windZ, gust;
				float time;
				uint32_t dropCount;
				float originX, originY;
				float originZ, maxSpeed, depthCorrection, frameDt;
				float foamAmount, foamLifetime, sprayAmount, dropletSize;
				uint32_t particleCount, particleFirstVertex, particleFirstPrevious, frameIndex;
				float windSpeed, windAngle, pad0, pad1;
			};
			static_assert(sizeof(Constants) == 128, "Constants must match water.slang");

			uint32_t Groups(uint32_t threads) {
				return (threads + GROUP_SIZE - 1) / GROUP_SIZE;
			}
		}

		std::unique_ptr<Fox::Graphics::Vulkan::Buffer> WaterSimulation::MakeBuffer(VkDeviceSize size, const void* data) {
			size = std::max<VkDeviceSize>(size, 16);
			auto buffer = std::make_unique<Fox::Graphics::Vulkan::Buffer>(device, physicalDevice, size,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			Fox::Graphics::Vulkan::Buffer staging(device, physicalDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			void* mapped = staging.Map();
			if (data) {
				std::memcpy(mapped, data, size);
			}
			else {
				std::memset(mapped, 0, size);
			}
			staging.Unmap();
			Fox::Graphics::Vulkan::CommandList commandList(device, commandPool);
			commandList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT).CopyBuffer(staging.Get(), buffer->Get(), { VkBufferCopy{ 0, 0, size } }).End().SubmitAndWait(queue);
			return buffer;
		}

		WaterSimulation::WaterSimulation(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue,
			const Grid& grid, const std::vector<float>& bedHeights, const std::vector<float>& initialDepth,
			uint32_t firstVertex, uint32_t firstPrevious, VkBuffer vertices, VkBuffer buildVertices, VkBuffer previousPositions,
			uint32_t particleCount, uint32_t particleFirstVertex, uint32_t particleFirstPrevious)
			: device(device), physicalDevice(physicalDevice), commandPool(commandPool), queue(queue), grid(grid),
			firstVertex(firstVertex), firstPrevious(firstPrevious),
			particleCount(particleCount), particleFirstVertex(particleFirstVertex), particleFirstPrevious(particleFirstPrevious)
		{
			const uint32_t cells = grid.width * grid.depth;
			const uint32_t uFaces = (grid.width + 1) * grid.depth;
			const uint32_t vFaces = grid.width * (grid.depth + 1);
			this->bedHeights = bedHeights;
			for (float d : initialDepth) {
				maxDepth = std::max(maxDepth, d);
				initialVolume += d;
			}
			initialVolume *= static_cast<double>(grid.cellSize) * grid.cellSize;

			bed = MakeBuffer(sizeof(float) * cells, bedHeights.data());
			for (int s = 0; s < 2; s++) {
				depth[s] = MakeBuffer(sizeof(float) * cells, initialDepth.data());
				u[s] = MakeBuffer(sizeof(float) * uFaces, nullptr);
				v[s] = MakeBuffer(sizeof(float) * vFaces, nullptr);
			}
			outflow = MakeBuffer(sizeof(float) * cells, nullptr);
			for (auto& field : foam) {
				field = MakeBuffer(sizeof(float) * cells, nullptr);
			}
			landed = MakeBuffer(sizeof(uint32_t) * cells, nullptr);
			churn = MakeBuffer(sizeof(float) * cells, nullptr);
			particles = MakeBuffer(sizeof(glm::vec4) * 2 * std::max(particleCount, 1u), nullptr); // Life 0: none flying
			particleCounter = MakeBuffer(sizeof(uint32_t) * 4, nullptr);
			drops = std::make_unique<Fox::Graphics::Vulkan::Buffer>(device, physicalDevice, sizeof(glm::vec4) * MAX_DROPS,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			using namespace Fox::Graphics::Vulkan;
			descriptors = std::make_unique<DescriptorSetBuilder>(device);
			for (uint32_t b = 0; b < BINDINGS; b++) {
				descriptors->AddBinding(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
			}
			descriptors->SetMaxSets(2).Build();
			layout = std::make_unique<PipelineLayout>(device, std::vector<VkDescriptorSetLayout>{ descriptors->GetLayout().Get() },
				std::vector<VkPushConstantRange>{ { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } });

			// Set s reads state s and writes state 1 - s
			for (int s = 0; s < 2; s++) {
				sets[s] = std::make_unique<DescriptorSet>(descriptors->AllocateSet());
				const VkBuffer buffers[BINDINGS] = {
					bed->Get(), depth[s]->Get(), depth[1 - s]->Get(), u[s]->Get(), u[1 - s]->Get(), v[s]->Get(), v[1 - s]->Get(),
					outflow->Get(), drops->Get(), vertices, buildVertices, previousPositions,
					foam[0]->Get(), foam[1]->Get(), landed->Get(), particles->Get(), particleCounter->Get(), churn->Get(),
				};
				VkDescriptorBufferInfo infos[BINDINGS];
				VkWriteDescriptorSet writes[BINDINGS];
				for (uint32_t b = 0; b < BINDINGS; b++) {
					infos[b] = { buffers[b], 0, VK_WHOLE_SIZE };
					writes[b] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					writes[b].dstSet = sets[s]->Get();
					writes[b].dstBinding = b;
					writes[b].descriptorCount = 1;
					writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					writes[b].pBufferInfo = &infos[b];
				}
				vkUpdateDescriptorSets(device, BINDINGS, writes, 0, nullptr);
			}

			for (int pass = 0; pass < PASSES; pass++) {
				const std::vector<char> code = Fox::Core::FileSystem::ReadBinaryFile(SHADERS[pass]);
				VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
				moduleInfo.codeSize = code.size();
				moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
				VkShaderModule module = VK_NULL_HANDLE;
				if (vkCreateShaderModule(device, &moduleInfo, nullptr, &module) != VK_SUCCESS) {
					throw std::runtime_error(std::string("Failed to create the shader module ") + SHADERS[pass]);
				}
				VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
				pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
				pipelineInfo.stage.module = module;
				pipelineInfo.stage.pName = "main";
				pipelineInfo.layout = layout->Get();
				const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipelines[pass]);
				vkDestroyShaderModule(device, module, nullptr);
				if (result != VK_SUCCESS) {
					throw std::runtime_error(std::string("Failed to create the pipeline of ") + SHADERS[pass]);
				}
			}
		}

		WaterSimulation::~WaterSimulation() {
			for (VkPipeline pipeline : pipelines) {
				if (pipeline) {
					vkDestroyPipeline(device, pipeline, nullptr);
				}
			}
		}

		void WaterSimulation::AddDrop(float x, float z, float radius, float amount) {
			if (pendingDrops.size() < MAX_DROPS) {
				// Narrower than two cells, a drop would mostly average away on the grid
				pendingDrops.emplace_back(x, z, std::max(radius, 2.0f * grid.cellSize), amount);
			}
		}

		void WaterSimulation::Barrier(VkCommandBuffer cmd) const {
			VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT };
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 1, &barrier, 0, nullptr, 0, nullptr);
		}

		void WaterSimulation::TakeReadback(uint32_t slot, uint32_t substeps, float dt) {
			if (!readbackWritten[slot]) {
				return;
			}
			const uint32_t cells = grid.width * grid.depth;
			const uint32_t uFaces = (grid.width + 1) * grid.depth;
			const uint32_t vFaces = grid.width * (grid.depth + 1);
			const float* values = static_cast<const float*>(readback[slot]->Map());
			cpuDepth.assign(values, values + cells);
			cpuU.assign(values + cells, values + cells + uFaces);
			cpuV.assign(values + cells + uFaces, values + cells + uFaces + vFaces);
			readback[slot]->Unmap();

			// The volume, every second or so: what it lost is put back over the next second
			if (frame % 60 == 0) {
				double volume = 0.0;
				uint32_t wet = 0;
				float low = 1e9f, high = -1e9f;
				for (float d : cpuDepth) {
					volume += d;
					wet += d > 1e-3f ? 1 : 0;
					low = std::min(low, d);
					high = std::max(high, d);
				}
				volume *= static_cast<double>(grid.cellSize) * grid.cellSize;
				const float wetArea = wet * grid.cellSize * grid.cellSize;
				correction = wetArea > 0.0f ? static_cast<float>((initialVolume - volume) / wetArea) : 0.0f;
				static const bool debug = SDL_getenv("FOX_WATER_DEBUG") != nullptr;
				if (debug) {
					std::cout << "Water: depth " << low << " .. " << high << ", volume " << volume << " of " << initialVolume << " m3, "
						<< substeps << " steps of " << dt * 1000.0f << " ms" << std::endl;
				}
			}
		}

		bool WaterSimulation::SampleSurface(float worldX, float worldZ, float& height, glm::vec3& normal, glm::vec2& velocity) const {
			if (cpuDepth.empty()) {
				return false;
			}
			const int W = static_cast<int>(grid.width), D = static_cast<int>(grid.depth);
			const float fx = (worldX - grid.origin.x) / grid.cellSize, fz = (worldZ - grid.origin.z) / grid.cellSize;
			if (fx < 0.0f || fz < 0.0f || fx >= W || fz >= D) {
				return false;
			}
			const int i = std::min(static_cast<int>(fx), W - 1), j = std::min(static_cast<int>(fz), D - 1);
			auto cell = [&](int ci, int cj) { return static_cast<size_t>(std::clamp(cj, 0, D - 1)) * W + std::clamp(ci, 0, W - 1); };
			const float h = cpuDepth[cell(i, j)];
			if (h <= 1e-3f) {
				return false;
			}
			// The level, bilinear between cell centres (dry neighbours count at this cell's level)
			auto level = [&](int ci, int cj) {
				const size_t c = cell(ci, cj);
				return cpuDepth[c] > 1e-3f ? bedHeights[c] + cpuDepth[c] : bedHeights[cell(i, j)] + h;
			};
			const float cx = fx - 0.5f, cz = fz - 0.5f;
			const int i0 = static_cast<int>(std::floor(cx)), j0 = static_cast<int>(std::floor(cz));
			const float tx = cx - i0, tz = cz - j0;
			height = glm::mix(glm::mix(level(i0, j0), level(i0 + 1, j0), tx), glm::mix(level(i0, j0 + 1), level(i0 + 1, j0 + 1), tx), tz);
			const float slopeX = (level(i + 1, j) - level(i - 1, j)) / (2.0f * grid.cellSize);
			const float slopeZ = (level(i, j + 1) - level(i, j - 1)) / (2.0f * grid.cellSize);
			normal = glm::normalize(glm::vec3(-slopeX, 1.0f, -slopeZ));
			// The cell's velocity: the average of its faces
			const size_t su = grid.width + 1;
			velocity.x = 0.5f * (cpuU[j * su + i] + cpuU[j * su + i + 1]);
			velocity.y = 0.5f * (cpuV[static_cast<size_t>(j) * W + i] + cpuV[static_cast<size_t>(j + 1) * W + i]);
			return true;
		}

		void WaterSimulation::Record(VkCommandBuffer cmd, float deltaTime, const Parameters& parameters, uint32_t frameSlot) {
			// The step a wave can take without skipping a cell (CFL), the speed of a wave in water this deep being
			// sqrt(g h), and the step diffusion stays stable at
			const float frameTime = std::min(deltaTime, 1.0f / 20.0f) * parameters.timeScale;
			const float waveSpeed = std::sqrt(GRAVITY * maxDepth) + 2.0f;
			float maxStep = 0.35f * grid.cellSize / waveSpeed;
			if (parameters.viscosity > 0.0f) {
				maxStep = std::min(maxStep, 0.2f * grid.cellSize * grid.cellSize / parameters.viscosity);
			}
			const uint32_t substeps = frameTime <= 0.0f ? 0u : std::clamp(static_cast<uint32_t>(std::ceil(frameTime / maxStep)), 1u, 16u);
			const float dt = substeps > 0 ? frameTime / static_cast<float>(substeps) : 0.0f;
			lastSubsteps = substeps;

			// The previous frame's rays and structure builds are done with the surface it is about to rewrite
			VkMemoryBarrier before{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT };
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);

			// This frame's drops, applied in its first step
			const uint32_t dropCount = static_cast<uint32_t>(pendingDrops.size());
			if (dropCount > 0) {
				vkCmdUpdateBuffer(cmd, drops->Get(), 0, sizeof(glm::vec4) * dropCount, pendingDrops.data());
				pendingDrops.clear();
				Barrier(cmd);
			}

			Constants constants{};
			constants.width = grid.width;
			constants.depth = grid.depth;
			constants.firstVertex = firstVertex;
			constants.firstPrevious = firstPrevious;
			constants.cellSize = grid.cellSize;
			constants.dt = dt;
			constants.gravity = GRAVITY;
			constants.viscosity = parameters.viscosity;
			constants.friction = parameters.friction;
			constants.windX = parameters.wind.x;
			constants.windZ = parameters.wind.y;
			constants.gust = parameters.gust;
			constants.originX = grid.origin.x;
			constants.originY = grid.origin.y;
			constants.originZ = grid.origin.z;
			constants.maxSpeed = 0.9f * grid.cellSize / std::max(dt, 1e-4f);
			constants.depthCorrection = correction * dt;
			constants.frameDt = frameTime;
			constants.foamAmount = parameters.foamAmount;
			constants.foamLifetime = parameters.foamLifetime;
			constants.sprayAmount = parameters.sprayAmount;
			constants.dropletSize = parameters.dropletSize;
			constants.particleCount = particleCount;
			constants.particleFirstVertex = particleFirstVertex;
			constants.particleFirstPrevious = particleFirstPrevious;
			constants.frameIndex = frame;
			constants.windSpeed = parameters.windSpeed;
			constants.windAngle = parameters.windAngle;

			const uint32_t cells = grid.width * grid.depth;
			const uint32_t faces = (grid.width + 1) * grid.depth + grid.width * (grid.depth + 1);

			// What this frame slot read back last time it ran (that frame has finished)
			const uint32_t slot = frameSlot % READBACK_SLOTS;
			TakeReadback(slot, substeps, dt);
			auto dispatch = [&](int pass, int set, uint32_t threads) {
				const VkDescriptorSet descriptorSet = sets[set]->Get();
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[pass]);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout->Get(), 0, 1, &descriptorSet, 0, nullptr);
				vkCmdPushConstants(cmd, layout->Get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &constants);
				vkCmdDispatch(cmd, Groups(threads), 1, 1);
				Barrier(cmd);
			};

			for (uint32_t step = 0; step < substeps; step++) {
				time += dt;
				constants.time = time;
				constants.dropCount = step == 0 ? dropCount : 0u;
				// Reads state `current`, writes the other: the velocities, then the outflow limits and the depth
				dispatch(0, current, faces);
				dispatch(1, current, cells);
				dispatch(2, current, cells);
				current = 1 - current;
			}

			// Foam (carried, fading, made where the water breaks), then the spray it throws up, then the droplets
			if (frameTime > 0.0f) {
				constants.dropCount = dropCount;
				dispatch(4, current, cells);
				const VkBufferCopy copy{ 0, 0, sizeof(float) * cells };
				vkCmdCopyBuffer(cmd, foam[1]->Get(), foam[0]->Get(), 1, &copy);
				Barrier(cmd);
				if (particleCount > 0) {
					dispatch(5, current, cells);
					dispatch(6, current, particleCount);
				}
			}

			// The surface, from the current depth and foam
			dispatch(3, current, cells);

			// Depth and velocities, read back for the CPU (taken when this frame slot comes round again)
			{
				const uint32_t uFaces = (grid.width + 1) * grid.depth;
				const uint32_t vFaces = grid.width * (grid.depth + 1);
				if (!readback[slot]) {
					readback[slot] = std::make_unique<Fox::Graphics::Vulkan::Buffer>(device, physicalDevice, sizeof(float) * (cells + uFaces + vFaces),
						VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				}
				const VkBufferCopy copies[3] = {
					{ 0, 0, sizeof(float) * cells },
					{ 0, sizeof(float) * cells, sizeof(float) * uFaces },
					{ 0, sizeof(float) * (cells + uFaces), sizeof(float) * vFaces },
				};
				vkCmdCopyBuffer(cmd, depth[current]->Get(), readback[slot]->Get(), 1, &copies[0]);
				vkCmdCopyBuffer(cmd, u[current]->Get(), readback[slot]->Get(), 1, &copies[1]);
				vkCmdCopyBuffer(cmd, v[current]->Get(), readback[slot]->Get(), 1, &copies[2]);
				VkMemoryBarrier toHost{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT };
				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0, nullptr, 0, nullptr);
				readbackWritten[slot] = true;
			}
			frame++;

			// The structure refit and the ray tracing shaders read the new vertices
			VkMemoryBarrier after{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR };
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &after, 0, nullptr, 0, nullptr);
		}
	}
}
