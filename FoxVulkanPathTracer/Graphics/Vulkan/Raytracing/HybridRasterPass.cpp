// The hybrid path tracer's rasterized first hits: cluster culling, the visibility buffer and the depth pyramid
// (see HybridRasterPass.h)

#include "FoxRenderer.h"

#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/HybridRasterPass.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				namespace {
					const uint32_t GROUP_SIZE = 64; // numthreads of hybrid_cull.slang
					const uint32_t CULL_BINDINGS = 16;
					const VkFormat VISIBILITY_FORMAT = VK_FORMAT_R32G32_UINT;
					const VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
					const VkFormat PYRAMID_FORMAT = VK_FORMAT_R32_SFLOAT;

					static_assert(sizeof(HybridRasterPass::Chunk) == 32, "Chunk must match hybrid_common.slang");
					static_assert(sizeof(HybridRasterPass::RasterInstance) == 32, "RasterInstance must match hybrid_common.slang");
					static_assert(sizeof(HybridRasterPass::DrawInfo) == 16, "DrawInfo must match hybrid_common.slang");
					static_assert(sizeof(HybridRasterPass::CullConstants) == 304, "CullConstants must match hybrid_common.slang");
					static_assert(sizeof(VkAccelerationStructureInstanceKHR) == 64, "TlasInstance is 64 bytes");

					void Barrier(VkCommandBuffer cmd, VkPipelineStageFlags from, VkAccessFlags fromAccess, VkPipelineStageFlags to, VkAccessFlags toAccess) {
						VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, fromAccess, toAccess };
						vkCmdPipelineBarrier(cmd, from, to, 0, 1, &barrier, 0, nullptr, 0, nullptr);
					}

					// Groups of a dispatch over more than 65535 of them (the y dimension carries the rest)
					void Dispatch(VkCommandBuffer cmd, uint32_t groups) {
						if (groups == 0) return;
						const uint32_t x = std::min(groups, 65535u);
						const uint32_t y = (groups + 65534u) / 65535u;
						vkCmdDispatch(cmd, x, y, 1);
					}

					glm::vec4 Plane(const glm::vec4& p) {
						const float length = glm::length(glm::vec3(p));
						return length > 0.0f ? p / length : p;
					}
				}

				HybridRasterPass::HybridRasterPass(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue,
					VkDescriptorSetLayout bindlessLayout, uint32_t framesInFlight)
					: device(device), physicalDevice(physicalDevice), commandPool(commandPool), queue(queue)
				{
					// --- Samplers: depth pyramid (nearest), the previous frame's image (bilinear) ---
					VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
					samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_NEAREST;
					samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
					samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
					samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
					vkCreateSampler(device, &samplerInfo, nullptr, &pointSampler);
					samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
					samplerInfo.maxLod = 0.0f;
					vkCreateSampler(device, &samplerInfo, nullptr, &linearSampler);

					// --- Descriptor layouts ---
					auto makeLayout = [&](const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
						VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
						info.bindingCount = static_cast<uint32_t>(bindings.size());
						info.pBindings = bindings.data();
						VkDescriptorSetLayout layout = VK_NULL_HANDLE;
						if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS) {
							throw std::runtime_error("HybridRasterPass: descriptor set layout");
						}
						return layout;
					};
					{
						std::vector<VkDescriptorSetLayoutBinding> bindings;
						bindings.push_back({ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
						for (uint32_t b = 1; b <= 12; b++) {
							bindings.push_back({ b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
						}
						bindings.push_back({ 13, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
						bindings.push_back({ 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
						bindings.push_back({ 15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
						cullLayout = makeLayout(bindings);
					}
					{
						const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
						std::vector<VkDescriptorSetLayoutBinding> bindings;
						for (uint32_t b : { 3u, 4u, 5u, 6u, 20u, 21u }) {
							bindings.push_back({ b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr });
						}
						rasterLayout = makeLayout(bindings);
					}
					{
						std::vector<VkDescriptorSetLayoutBinding> bindings;
						for (uint32_t b : { 3u, 4u, 5u, 6u, 20u, 21u, 22u, 23u }) {
							bindings.push_back({ b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
						}
						softwareLayout = makeLayout(bindings);
					}
					resolveLayout = makeLayout({
						{ 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
						{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
						{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
						{ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
						{ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr } });
					pyramidLayout = makeLayout({
						{ 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
						{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr } });

					const uint32_t maxPyramidLevels = 16;
					const VkDescriptorPoolSize sizes[] = {
						{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, framesInFlight },
						{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, framesInFlight * 14 + 6 + 8 + 2 },
						{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, framesInFlight + maxPyramidLevels + 1 },
						{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxPyramidLevels + 2 },
					};
					VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
					poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
					poolInfo.maxSets = framesInFlight + 3 + maxPyramidLevels;
					poolInfo.poolSizeCount = 4;
					poolInfo.pPoolSizes = sizes;
					vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool);

					auto allocate = [&](VkDescriptorSetLayout layout) {
						VkDescriptorSetAllocateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
						info.descriptorPool = pool;
						info.descriptorSetCount = 1;
						info.pSetLayouts = &layout;
						VkDescriptorSet set = VK_NULL_HANDLE;
						if (vkAllocateDescriptorSets(device, &info, &set) != VK_SUCCESS) {
							throw std::runtime_error("HybridRasterPass: descriptor set");
						}
						return set;
					};

					// --- Pipelines ---
					CreatePipelines(bindlessLayout);

					// --- Buffers ---
					const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
					draws = MakeBuffer(512, storage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT); // 16 draws, then the software dispatches
					drawInfos = MakeBuffer(sizeof(DrawInfo) * (BUCKETS + 2) * BUCKET_CAPACITY, storage);
					counters = MakeBuffer(sizeof(uint32_t) * COUNTERS, storage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
					candidates = MakeBuffer(sizeof(uint32_t) * 2 * MAX_CANDIDATES, storage);
					tlasCopy = MakeBuffer(sizeof(VkAccelerationStructureInstanceKHR) * MAX_INSTANCES, storage);
					dummy = MakeBuffer(256, storage);
					largeInstances = MakeBuffer(sizeof(uint32_t) * LARGE_CAPACITY, storage);
					chunks = MakeBuffer(sizeof(Chunk), storage);
					rasterInstances = MakeBuffer(sizeof(RasterInstance), storage);

					frames.resize(framesInFlight);
					for (Frame& frame : frames) {
						frame.constants = MakeBuffer(sizeof(CullConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
						frame.stats = MakeBuffer(sizeof(uint32_t) * COUNTERS, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
						frame.cullSet = allocate(cullLayout);
					}
					rasterSet = allocate(rasterLayout);
					softwareSet = allocate(softwareLayout);
					resolveSet = allocate(resolveLayout);
					pyramidSets.resize(maxPyramidLevels);
					for (VkDescriptorSet& set : pyramidSets) {
						set = allocate(pyramidLayout);
					}
				}

				HybridRasterPass::~HybridRasterPass() {
					DestroyImage(visibility);
					DestroyImage(depth);
					DestroyImage(pyramid);
					for (VkPipeline pipeline : cullPipelines) vkDestroyPipeline(device, pipeline, nullptr);
					for (VkPipeline pipeline : rasterPipelines) vkDestroyPipeline(device, pipeline, nullptr);
					vkDestroyPipeline(device, pyramidPipeline, nullptr);
					vkDestroyPipeline(device, softwarePipeline, nullptr);
					vkDestroyPipeline(device, resolvePipeline, nullptr);
					vkDestroyPipelineLayout(device, softwarePipelineLayout, nullptr);
					vkDestroyPipelineLayout(device, resolvePipelineLayout, nullptr);
					vkDestroyDescriptorSetLayout(device, softwareLayout, nullptr);
					vkDestroyDescriptorSetLayout(device, resolveLayout, nullptr);
					vkDestroyPipelineLayout(device, cullPipelineLayout, nullptr);
					vkDestroyPipelineLayout(device, rasterPipelineLayout, nullptr);
					vkDestroyPipelineLayout(device, pyramidPipelineLayout, nullptr);
					vkDestroyDescriptorPool(device, pool, nullptr);
					vkDestroyDescriptorSetLayout(device, cullLayout, nullptr);
					vkDestroyDescriptorSetLayout(device, rasterLayout, nullptr);
					vkDestroyDescriptorSetLayout(device, pyramidLayout, nullptr);
					vkDestroySampler(device, pointSampler, nullptr);
					vkDestroySampler(device, linearSampler, nullptr);
				}

				VkBuffer HybridRasterPass::GetDrawInfos() const { return drawInfos->Get(); }
				VkBuffer HybridRasterPass::GetInstances() const { return tlasCopy->Get(); }

				std::unique_ptr<Buffer> HybridRasterPass::MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
					size = std::max<VkDeviceSize>(size, 256);
					memory += size;
					return std::make_unique<Buffer>(device, physicalDevice, size, usage,
						hostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				}

				void HybridRasterPass::UploadTo(Buffer& buffer, const void* data, VkDeviceSize size) {
					if (size == 0) return;
					Buffer staging(device, physicalDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
					std::memcpy(staging.Map(), data, size);
					staging.Unmap();
					CommandList commandList(device, commandPool);
					commandList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT).CopyBuffer(staging.Get(), buffer.Get(), { VkBufferCopy{ 0, 0, size } }).End().SubmitAndWait(queue);
				}

				VkShaderModule HybridRasterPass::LoadShader(const char* path) {
					const std::vector<char> code = Fox::Core::FileSystem::ReadBinaryFile(path);
					VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
					info.codeSize = code.size();
					info.pCode = reinterpret_cast<const uint32_t*>(code.data());
					VkShaderModule module = VK_NULL_HANDLE;
					if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
						throw std::runtime_error(std::string("Failed to create the shader module ") + path);
					}
					return module;
				}

				void HybridRasterPass::CreatePipelines(VkDescriptorSetLayout bindlessLayout) {
					auto pipelineLayout = [&](std::vector<VkDescriptorSetLayout> sets, VkShaderStageFlags pushStages, uint32_t pushSize) {
						VkPushConstantRange range{ pushStages, 0, pushSize };
						VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
						info.setLayoutCount = static_cast<uint32_t>(sets.size());
						info.pSetLayouts = sets.data();
						info.pushConstantRangeCount = pushSize > 0 ? 1 : 0;
						info.pPushConstantRanges = &range;
						VkPipelineLayout layout = VK_NULL_HANDLE;
						vkCreatePipelineLayout(device, &info, nullptr, &layout);
						return layout;
					};
					cullPipelineLayout = pipelineLayout({ cullLayout }, 0, 0);
					rasterPipelineLayout = pipelineLayout({ rasterLayout, bindlessLayout }, VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::vec4) * 4);
					pyramidPipelineLayout = pipelineLayout({ pyramidLayout }, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(uint32_t) * 4);

					auto compute = [&](const char* path, VkPipelineLayout layout) {
						VkShaderModule module = LoadShader(path);
						VkComputePipelineCreateInfo info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
						info.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main" };
						info.layout = layout;
						VkPipeline pipeline = VK_NULL_HANDLE;
						const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
						vkDestroyShaderModule(device, module, nullptr);
						if (result != VK_SUCCESS) {
							throw std::runtime_error(std::string("Failed to create the pipeline of ") + path);
						}
						return pipeline;
					};
					cullPipelines[0] = compute("Shaders/hybrid_cull_instances.spv", cullPipelineLayout);
					cullPipelines[1] = compute("Shaders/hybrid_cull_clusters.spv", cullPipelineLayout);
					cullPipelines[2] = compute("Shaders/hybrid_cull_candidates.spv", cullPipelineLayout);
					cullPipelines[3] = compute("Shaders/hybrid_cull_large.spv", cullPipelineLayout);
					pyramidPipeline = compute("Shaders/hybrid_hiz.spv", pyramidPipelineLayout);
					softwarePipelineLayout = pipelineLayout({ softwareLayout, bindlessLayout }, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(glm::vec4) * 5);
					softwarePipeline = compute("Shaders/hybrid_swraster.spv", softwarePipelineLayout);
					resolvePipelineLayout = pipelineLayout({ resolveLayout }, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(uint32_t) * 2);
					resolvePipeline = compute("Shaders/hybrid_resolve.spv", resolvePipelineLayout);

					// The visibility buffer: no vertex input (the vertex shader reads the scene's buffers), double sided
					// (as the rays see every surface), reversed depth
					VkShaderModule vertex = LoadShader("Shaders/hybrid_raster_vert.spv");
					const char* fragments[2] = { "Shaders/hybrid_raster_frag.spv", "Shaders/hybrid_raster_frag_masked.spv" };
					for (int masked = 0; masked < 2; masked++) {
						VkShaderModule fragment = LoadShader(fragments[masked]);
						VkPipelineShaderStageCreateInfo stages[2] = {
							{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main" },
							{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main" },
						};
						VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
						VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
						assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
						VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
						viewport.viewportCount = 1;
						viewport.scissorCount = 1;
						VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
						raster.polygonMode = VK_POLYGON_MODE_FILL;
						raster.cullMode = VK_CULL_MODE_NONE;
						raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
						raster.lineWidth = 1.0f;
						VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
						multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
						VkPipelineDepthStencilStateCreateInfo depthState{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
						depthState.depthTestEnable = VK_TRUE;
						depthState.depthWriteEnable = VK_TRUE;
						depthState.depthCompareOp = VK_COMPARE_OP_GREATER;
						VkPipelineColorBlendAttachmentState blendAttachment{};
						blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
						VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
						blend.attachmentCount = 1;
						blend.pAttachments = &blendAttachment;
						const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
						VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
						dynamic.dynamicStateCount = 2;
						dynamic.pDynamicStates = dynamicStates;
						VkPipelineRenderingCreateInfo rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
						rendering.colorAttachmentCount = 1;
						rendering.pColorAttachmentFormats = &VISIBILITY_FORMAT;
						rendering.depthAttachmentFormat = DEPTH_FORMAT;

						VkGraphicsPipelineCreateInfo info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering };
						info.stageCount = 2;
						info.pStages = stages;
						info.pVertexInputState = &vertexInput;
						info.pInputAssemblyState = &assembly;
						info.pViewportState = &viewport;
						info.pRasterizationState = &raster;
						info.pMultisampleState = &multisample;
						info.pDepthStencilState = &depthState;
						info.pColorBlendState = &blend;
						info.pDynamicState = &dynamic;
						info.layout = rasterPipelineLayout;
						const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &rasterPipelines[masked]);
						vkDestroyShaderModule(device, fragment, nullptr);
						if (result != VK_SUCCESS) {
							vkDestroyShaderModule(device, vertex, nullptr);
							throw std::runtime_error("Failed to create the visibility buffer pipeline");
						}
					}
					vkDestroyShaderModule(device, vertex, nullptr);
				}

				HybridRasterPass::Image HybridRasterPass::MakeImage(VkExtent2D size, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t levels) {
					Image image;
					image.levelCount = levels;
					VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
					info.imageType = VK_IMAGE_TYPE_2D;
					info.format = format;
					info.extent = { size.width, size.height, 1 };
					info.mipLevels = levels;
					info.arrayLayers = 1;
					info.samples = VK_SAMPLE_COUNT_1_BIT;
					info.tiling = VK_IMAGE_TILING_OPTIMAL;
					info.usage = usage;
					info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					if (vkCreateImage(device, &info, nullptr, &image.image) != VK_SUCCESS) {
						throw std::runtime_error("HybridRasterPass: image");
					}
					VkMemoryRequirements requirements;
					vkGetImageMemoryRequirements(device, image.image, &requirements);
					VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
					allocate.allocationSize = requirements.size;
					allocate.memoryTypeIndex = Fox::Scene::RayTracing::RayTracingScene::FindDeviceLocalMemoryType(physicalDevice, requirements.memoryTypeBits);
					if (vkAllocateMemory(device, &allocate, nullptr, &image.memory) != VK_SUCCESS) {
						throw std::runtime_error("HybridRasterPass: image memory");
					}
					vkBindImageMemory(device, image.image, image.memory, 0);
					memory += requirements.size;

					VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
					viewInfo.image = image.image;
					viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
					viewInfo.format = format;
					viewInfo.subresourceRange = { aspect, 0, levels, 0, 1 };
					vkCreateImageView(device, &viewInfo, nullptr, &image.view);
					for (uint32_t level = 0; level < levels; level++) {
						viewInfo.subresourceRange = { aspect, level, 1, 0, 1 };
						VkImageView view = VK_NULL_HANDLE;
						vkCreateImageView(device, &viewInfo, nullptr, &view);
						image.levels.push_back(view);
					}
					return image;
				}

				void HybridRasterPass::DestroyImage(Image& image) {
					for (VkImageView view : image.levels) vkDestroyImageView(device, view, nullptr);
					if (image.view) vkDestroyImageView(device, image.view, nullptr);
					if (image.image) vkDestroyImage(device, image.image, nullptr);
					if (image.memory) vkFreeMemory(device, image.memory, nullptr);
					image = Image{};
				}

				void HybridRasterPass::Upload(const Content& content) {
					const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
					chunkCount = static_cast<uint32_t>(content.chunks.size());
					clusterChunkBase = content.clusterChunkBase;
					chunks = MakeBuffer(sizeof(Chunk) * std::max<size_t>(content.chunks.size(), 1), storage);
					UploadTo(*chunks, content.chunks.data(), sizeof(Chunk) * content.chunks.size());
					std::vector<RasterInstance> instances = content.instances;
					instances.resize(MAX_INSTANCES); // Instances added later (no content yet): no clusters
					rasterInstances = MakeBuffer(sizeof(RasterInstance) * instances.size(), storage);
					UploadTo(*rasterInstances, instances.data(), sizeof(RasterInstance) * instances.size());
					stats.chunks = chunkCount;
					pyramidValid = false;
				}

				void HybridRasterPass::Resize(VkExtent2D size) {
					if (size.width == extent.width && size.height == extent.height && visibility.image) {
						return;
					}
					extent = size;
					DestroyImage(visibility);
					DestroyImage(depth);
					DestroyImage(pyramid);
					const uint32_t levels = static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(size.width, size.height))))) + 1;
					visibility = MakeImage(size, VISIBILITY_FORMAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1);
					depth = MakeImage(size, DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
					pyramid = MakeImage(size, PYRAMID_FORMAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, levels);

					// Every image stays in the general layout: the passes only order their accesses with memory barriers
					CommandList commandList(device, commandPool);
					commandList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
					VkImageMemoryBarrier barriers[3]{};
					const Image* images[3] = { &visibility, &depth, &pyramid };
					const VkImageAspectFlags aspects[3] = { VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_ASPECT_COLOR_BIT };
					for (int i = 0; i < 3; i++) {
						barriers[i] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
						barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
						barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
						barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
						barriers[i].srcQueueFamilyIndex = barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
						barriers[i].image = images[i]->image;
						barriers[i].subresourceRange = { aspects[i], 0, images[i]->levelCount, 0, 1 };
					}
					vkCmdPipelineBarrier(commandList.Get(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
						0, 0, nullptr, 0, nullptr, 3, barriers);
					VkClearColorValue zero{};
					VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1 };
					vkCmdClearColorImage(commandList.Get(), pyramid.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &all);
					commandList.End().SubmitAndWait(queue);

					software = MakeBuffer(sizeof(uint64_t) * size.width * size.height, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
					{
						VkDescriptorImageInfo depthInfo{ VK_NULL_HANDLE, depth.view, VK_IMAGE_LAYOUT_GENERAL };
						VkDescriptorBufferInfo softwareInfo{ software->Get(), 0, VK_WHOLE_SIZE };
						VkDescriptorBufferInfo drawInfo{ drawInfos->Get(), 0, VK_WHOLE_SIZE };
						VkDescriptorImageInfo visibilityInfo{ VK_NULL_HANDLE, visibility.view, VK_IMAGE_LAYOUT_GENERAL };
						VkDescriptorImageInfo levelInfo{ VK_NULL_HANDLE, pyramid.levels[0], VK_IMAGE_LAYOUT_GENERAL };
						VkWriteDescriptorSet writes[5];
						const VkDescriptorType types[5] = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
							VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
						for (uint32_t b = 0; b < 5; b++) {
							writes[b] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
							writes[b].dstSet = resolveSet;
							writes[b].dstBinding = b;
							writes[b].descriptorCount = 1;
							writes[b].descriptorType = types[b];
						}
						writes[0].pImageInfo = &depthInfo;
						writes[1].pBufferInfo = &softwareInfo;
						writes[2].pBufferInfo = &drawInfo;
						writes[3].pImageInfo = &visibilityInfo;
						writes[4].pImageInfo = &levelInfo;
						vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
					}
					softwareBound = VK_NULL_HANDLE; // The software set is rewritten with the next frame
					std::fill(rasterBound, rasterBound + 4, VK_NULL_HANDLE);

					// The pyramid's passes: level 0 from the depth buffer, each next from the one below
					for (uint32_t level = 0; level < levels && level < pyramidSets.size(); level++) {
						VkDescriptorImageInfo source{ VK_NULL_HANDLE, level == 0 ? depth.view : pyramid.levels[level - 1], VK_IMAGE_LAYOUT_GENERAL };
						VkDescriptorImageInfo target{ VK_NULL_HANDLE, pyramid.levels[level], VK_IMAGE_LAYOUT_GENERAL };
						VkWriteDescriptorSet writes[2] = { { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }, { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET } };
						writes[0].dstSet = writes[1].dstSet = pyramidSets[level];
						writes[0].dstBinding = 0;
						writes[0].descriptorCount = 1;
						writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
						writes[0].pImageInfo = &source;
						writes[1].dstBinding = 1;
						writes[1].descriptorCount = 1;
						writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
						writes[1].pImageInfo = &target;
						vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
					}
					pyramidValid = false;
				}

				void HybridRasterPass::ViewProjection(const View& view, VkExtent2D size, bool jittered, glm::vec4 rows[4]) {
					// x right, y up, z forward (camera space as the primary rays: forward + x right + y up); Vulkan's
					// y points down the screen. Jittered: shifted so the pixel centres sample what the jittered rays hit.
					const float f = 1.0f / std::max(view.tanHalfFov, 1e-6f);
					const float kx = jittered ? -2.0f * view.jitter.x / static_cast<float>(size.width) : 0.0f;
					const float ky = jittered ? -2.0f * view.jitter.y / static_cast<float>(size.height) : 0.0f;
					const glm::vec3 x = view.right * (f / view.aspect) + view.forward * kx;
					const glm::vec3 y = -view.up * f + view.forward * ky;
					rows[0] = glm::vec4(x, -glm::dot(x, view.position));
					rows[1] = glm::vec4(y, -glm::dot(y, view.position));
					rows[2] = glm::vec4(0.0f, 0.0f, 0.0f, view.nearPlane); // Reversed depth: near / distance
					rows[3] = glm::vec4(view.forward, -glm::dot(view.forward, view.position));
				}

				void HybridRasterPass::WriteCullSet(Frame& frame, const FrameInput& input) {
					const VkBuffer buffers[13] = {
						frame.constants->Get(), chunks->Get(), rasterInstances->Get(), tlasCopy->Get(), input.instanceMotion, input.submeshes,
						draws->Get(), drawInfos->Get(), counters->Get(), candidates->Get(),
						input.clusterInstances ? input.clusterInstances : dummy->Get(),
						input.clusterCounts ? input.clusterCounts : dummy->Get(),
						input.clusterSelected ? input.clusterSelected : dummy->Get(),
					};
					VkDescriptorBufferInfo infos[13];
					VkWriteDescriptorSet writes[CULL_BINDINGS];
					for (uint32_t b = 0; b < 13; b++) {
						infos[b] = { buffers[b], 0, VK_WHOLE_SIZE };
						writes[b] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
						writes[b].dstSet = frame.cullSet;
						writes[b].dstBinding = b;
						writes[b].descriptorCount = 1;
						writes[b].descriptorType = b == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
						writes[b].pBufferInfo = &infos[b];
					}
					VkDescriptorBufferInfo largeInfo{ largeInstances->Get(), 0, VK_WHOLE_SIZE };
					writes[15] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					writes[15].dstSet = frame.cullSet;
					writes[15].dstBinding = 15;
					writes[15].descriptorCount = 1;
					writes[15].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					writes[15].pBufferInfo = &largeInfo;
					VkDescriptorBufferInfo lodInfo{ input.lodClusters ? input.lodClusters : dummy->Get(), 0, VK_WHOLE_SIZE };
					writes[14] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					writes[14].dstSet = frame.cullSet;
					writes[14].dstBinding = 14;
					writes[14].descriptorCount = 1;
					writes[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					writes[14].pBufferInfo = &lodInfo;
					VkDescriptorImageInfo pyramidInfo{ VK_NULL_HANDLE, pyramid.view, VK_IMAGE_LAYOUT_GENERAL };
					writes[13] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					writes[13].dstSet = frame.cullSet;
					writes[13].dstBinding = 13;
					writes[13].descriptorCount = 1;
					writes[13].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
					writes[13].pImageInfo = &pyramidInfo;
					vkUpdateDescriptorSets(device, CULL_BINDINGS, writes, 0, nullptr);
				}

				void HybridRasterPass::WriteRasterSet(const FrameInput& input) {
					// The scene's buffers change only with a rebuild (device idle); the set is rewritten then
					const VkBuffer scene[4] = { input.vertices, input.indices, input.submeshes, input.materials };
					if (std::equal(scene, scene + 4, rasterBound) && softwareBound == software->Get()) {
						return;
					}
					std::copy(scene, scene + 4, rasterBound);
					softwareBound = software->Get();
					{
						const uint32_t bindings[8] = { 3, 4, 5, 6, 20, 21, 22, 23 };
						const VkBuffer buffers[8] = { input.vertices, input.indices, input.submeshes, input.materials, drawInfos->Get(), tlasCopy->Get(),
							software->Get(), draws->Get() };
						VkDescriptorBufferInfo infos[8];
						VkWriteDescriptorSet writes[8];
						for (int i = 0; i < 8; i++) {
							infos[i] = { buffers[i], 0, VK_WHOLE_SIZE };
							writes[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
							writes[i].dstSet = softwareSet;
							writes[i].dstBinding = bindings[i];
							writes[i].descriptorCount = 1;
							writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
							writes[i].pBufferInfo = &infos[i];
						}
						vkUpdateDescriptorSets(device, 8, writes, 0, nullptr);
					}
					const uint32_t bindings[6] = { 3, 4, 5, 6, 20, 21 };
					const VkBuffer buffers[6] = { input.vertices, input.indices, input.submeshes, input.materials, drawInfos->Get(), tlasCopy->Get() };
					VkDescriptorBufferInfo infos[6];
					VkWriteDescriptorSet writes[6];
					for (int i = 0; i < 6; i++) {
						infos[i] = { buffers[i], 0, VK_WHOLE_SIZE };
						writes[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
						writes[i].dstSet = rasterSet;
						writes[i].dstBinding = bindings[i];
						writes[i].descriptorCount = 1;
						writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
						writes[i].pBufferInfo = &infos[i];
					}
					vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);
				}

				void HybridRasterPass::Cull(VkCommandBuffer cmd, VkPipeline pipeline, uint32_t groups, VkDescriptorSet set) {
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipelineLayout, 0, 1, &set, 0, nullptr);
					Dispatch(cmd, groups);
				}

				void HybridRasterPass::Draw(VkCommandBuffer cmd, uint32_t phase, bool clear, const glm::vec4 viewProj[4], VkDescriptorSet bindlessSet) {
					VkRenderingAttachmentInfo color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
					color.imageView = visibility.view;
					color.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
					color.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
					color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
					color.clearValue.color.uint32[0] = 0; // Nothing: sky
					VkRenderingAttachmentInfo depthAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
					depthAttachment.imageView = depth.view;
					depthAttachment.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
					depthAttachment.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
					depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
					depthAttachment.clearValue.depthStencil.depth = 0.0f; // Reversed: infinitely far
					VkRenderingInfo rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO };
					rendering.renderArea = { { 0, 0 }, extent };
					rendering.layerCount = 1;
					rendering.colorAttachmentCount = 1;
					rendering.pColorAttachments = &color;
					rendering.pDepthAttachment = &depthAttachment;
					vkCmdBeginRendering(cmd, &rendering);

					VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f };
					VkRect2D scissor{ { 0, 0 }, extent };
					vkCmdSetViewport(cmd, 0, 1, &viewport);
					vkCmdSetScissor(cmd, 0, 1, &scissor);
					const VkDescriptorSet sets[2] = { rasterSet, bindlessSet };
					for (uint32_t masked = 0; masked < 2; masked++) {
						vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipelines[masked]);
						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipelineLayout, 0, 2, sets, 0, nullptr);
						vkCmdPushConstants(cmd, rasterPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec4) * 4, viewProj);
						const uint32_t list = phase * 2 + masked;
						vkCmdDrawIndirect(cmd, draws->Get(), sizeof(VkDrawIndirectCommand) * list * BUCKETS_PER_LIST, BUCKETS_PER_LIST,
							sizeof(VkDrawIndirectCommand));
					}
					vkCmdEndRendering(cmd);
				}

				void HybridRasterPass::RasterizeSoftware(VkCommandBuffer cmd, uint32_t phase, const glm::vec4 viewProj[4], VkDescriptorSet bindlessSet) {
					struct {
						glm::vec4 viewProj[4];
						glm::vec2 size;
						uint32_t firstRecord;
						uint32_t countOffset;
					} constants;
					for (int i = 0; i < 4; i++) constants.viewProj[i] = viewProj[i];
					constants.size = glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
					constants.firstRecord = (BUCKETS + phase) * BUCKET_CAPACITY;
					constants.countOffset = SOFTWARE_DISPATCH + phase * 3;
					static_assert(sizeof(constants) == sizeof(glm::vec4) * 5, "Software raster constants");
					const VkDescriptorSet sets[2] = { softwareSet, bindlessSet };
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, softwarePipeline);
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, softwarePipelineLayout, 0, 2, sets, 0, nullptr);
					vkCmdPushConstants(cmd, softwarePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
					vkCmdDispatchIndirect(cmd, draws->Get(), sizeof(uint32_t) * (SOFTWARE_DISPATCH + phase * 3));
				}

				void HybridRasterPass::Resolve(VkCommandBuffer cmd) {
					const uint32_t size[2] = { extent.width, extent.height };
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resolvePipeline);
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, resolvePipelineLayout, 0, 1, &resolveSet, 0, nullptr);
					vkCmdPushConstants(cmd, resolvePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(size), size);
					vkCmdDispatch(cmd, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
					Barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
				}

				void HybridRasterPass::BuildPyramid(VkCommandBuffer cmd) {
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pyramidPipeline);
					uint32_t sourceWidth = extent.width, sourceHeight = extent.height;
					// Level 0: the resolve's (the nearer of the hardware's and the software rasterizer's depth)
					for (uint32_t level = 1; level < pyramid.levelCount && level < pyramidSets.size(); level++) {
						const uint32_t width = std::max(1u, (sourceWidth + 1) / 2);
						const uint32_t height = std::max(1u, (sourceHeight + 1) / 2);
						const uint32_t sizes[4] = { width, height, sourceWidth, sourceHeight };
						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pyramidPipelineLayout, 0, 1, &pyramidSets[level], 0, nullptr);
						vkCmdPushConstants(cmd, pyramidPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(sizes), sizes);
						vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
						Barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
						sourceWidth = width;
						sourceHeight = height;
					}
				}

				void HybridRasterPass::Record(VkCommandBuffer cmd, uint32_t frameSlot, const FrameInput& input, VkDescriptorSet bindlessSet,
					const std::function<void(const char*)>& mark) {
					if (!visibility.image || frameSlot >= frames.size()) {
						return;
					}
					Frame& frame = frames[frameSlot];

					// The previous frame's path tracer may still read the images and buffers
					Barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
						VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);

					const uint32_t instanceCount = std::min(input.instanceCount, MAX_INSTANCES);
					if (input.tlasInstancesWritten && instanceCount > 0) {
						VkBufferCopy copy{ 0, 0, sizeof(VkAccelerationStructureInstanceKHR) * instanceCount };
						vkCmdCopyBuffer(cmd, input.tlasInstances, tlasCopy->Get(), 1, &copy);
					}
					vkCmdFillBuffer(cmd, counters->Get(), 0, VK_WHOLE_SIZE, 0);
					// Each bucket's draw: its corners, no instances yet (the culling adds them), its records' first index
					struct {
						VkDrawIndirectCommand draws[BUCKETS];
						uint32_t dispatches[6]; // Software: x (clusters), y, z per phase
					} commands{};
					static_assert(sizeof(commands.draws) == sizeof(uint32_t) * SOFTWARE_DISPATCH, "The dispatches follow the draws");
					for (uint32_t b = 0; b < BUCKETS; b++) {
						commands.draws[b] = { BUCKET_SIZES[b % BUCKETS_PER_LIST] * 3, 0, 0, b * BUCKET_CAPACITY };
					}
					for (int phase = 0; phase < 2; phase++) {
						commands.dispatches[phase * 3 + 1] = 1;
						commands.dispatches[phase * 3 + 2] = 1;
					}
					vkCmdUpdateBuffer(cmd, draws->Get(), 0, sizeof(commands), &commands);
					vkCmdFillBuffer(cmd, software->Get(), 0, VK_WHOLE_SIZE, 0);
					if (mark) mark("raster setup");

					// --- This frame's view ---
					CullConstants constants{};
					glm::vec4 current[4], unjittered[4];
					ViewProjection(input.view, extent, true, current);
					ViewProjection(input.view, extent, false, unjittered);
					const bool previousValid = pyramidValid && !input.resetHistory;
					for (int i = 0; i < 4; i++) {
						constants.viewProj[i] = current[i];
						constants.prevViewProj[i] = previousValid ? previousViewProj[i] : current[i];
					}
					constants.planes[0] = Plane(unjittered[3] + unjittered[0]);
					constants.planes[1] = Plane(unjittered[3] - unjittered[0]);
					constants.planes[2] = Plane(unjittered[3] + unjittered[1]);
					constants.planes[3] = Plane(unjittered[3] - unjittered[1]);
					constants.planes[4] = Plane(unjittered[3] - glm::vec4(0.0f, 0.0f, 0.0f, input.view.nearPlane));
					constants.planes[5] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // No far plane
					constants.cameraPosition = glm::vec4(input.view.position, input.view.nearPlane);
					constants.hiz = glm::vec4(static_cast<float>(extent.width), static_cast<float>(extent.height), static_cast<float>(pyramid.levelCount), previousValid ? 1.0f : 0.0f);
					constants.counts = glm::uvec4(instanceCount, input.clusterInstanceCount, clusterChunkBase, BUCKET_CAPACITY);
					constants.limits = glm::uvec4(MAX_CANDIDATES, input.occlusionCulling ? 1u : 0u, CHUNK_TRIANGLES, BUCKET_CAPACITY);
					const float pixelScale = static_cast<float>(extent.height) / (2.0f * std::max(input.view.tanHalfFov, 1e-6f));
					constants.lod = glm::vec4(input.lodClusters ? input.lodPixelScale : 0.0f, input.lodThresholdScale, pixelScale, input.softwareLimit);
					std::memcpy(frame.constants->Map(), &constants, sizeof(constants));
					frame.constants->Unmap();
					WriteCullSet(frame, input);
					WriteRasterSet(input);

					Barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

					const VkPipelineStageFlags drawStages = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
					const VkAccessFlags drawReads = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
					const VkPipelineStageFlags attachments = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
					const VkAccessFlags attachmentWrites = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

					// --- Phase 1: cull against the previous frame's pyramid, draw what was visible ---
					// A thread per instance; those with many clusters then a group each
					Cull(cmd, cullPipelines[0], (instanceCount + GROUP_SIZE - 1) / GROUP_SIZE, frame.cullSet);
					Barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
					Cull(cmd, cullPipelines[3], LARGE_CAPACITY, frame.cullSet);
					if (mark) mark("cull");
					if (input.clusterInstanceCount > 0) {
						Cull(cmd, cullPipelines[1], input.clusterInstanceCount, frame.cullSet);
						if (mark) mark("cull trees");
					}
					Barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, drawStages | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, drawReads);
					Draw(cmd, 0, true, current, bindlessSet);
					RasterizeSoftware(cmd, 0, current, bindlessSet);
					if (mark) mark("raster");

					// --- The pyramid of what phase 1 drew; phase 2: the hidden ones that came into view ---
					Barrier(cmd, attachments | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, attachmentWrites | VK_ACCESS_SHADER_WRITE_BIT,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
					Resolve(cmd);
					BuildPyramid(cmd);
					Cull(cmd, cullPipelines[2], (MAX_CANDIDATES + GROUP_SIZE - 1) / GROUP_SIZE, frame.cullSet);
					Barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
						drawStages | attachments, drawReads | attachmentWrites);
					Draw(cmd, 1, false, current, bindlessSet);
					RasterizeSoftware(cmd, 1, current, bindlessSet);

					// --- The final pyramid: the next frame culls against it, the screen traces march through it ---
					Barrier(cmd, attachments | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, attachmentWrites | VK_ACCESS_SHADER_WRITE_BIT,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
					Resolve(cmd);
					BuildPyramid(cmd);
					if (mark) mark("occlusion");

					Barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | attachments, VK_ACCESS_SHADER_WRITE_BIT | attachmentWrites,
						VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT);
					VkBufferCopy copy{ 0, 0, sizeof(uint32_t) * COUNTERS };
					vkCmdCopyBuffer(cmd, counters->Get(), frame.stats->Get(), 1, &copy);
					frame.statsPending = true;

					for (int i = 0; i < 4; i++) {
						previousViewProj[i] = current[i];
					}
					pyramidValid = true;
				}

				void HybridRasterPass::ReadStats(uint32_t frameSlot) {
					if (frameSlot >= frames.size() || !frames[frameSlot].statsPending) {
						return;
					}
					Frame& frame = frames[frameSlot];
					uint32_t values[COUNTERS];
					std::memcpy(values, frame.stats->Map(), sizeof(values));
					frame.stats->Unmap();
					frame.statsPending = false;
					stats.drawsPhase1 = values[0] + values[1];
					stats.drawsPhase2 = values[2] + values[3];
					stats.clustersTested = values[5];
					stats.instancesVisible = values[6];
					stats.triangles = values[7];
					stats.clustersOccluded = values[8];
					stats.clustersOutside = values[9];
					stats.overflow = values[10];
					stats.clustersRevealed = stats.drawsPhase2;
					for (int list = 0; list < 4; list++) {
						stats.listDraws[list] = values[list];
						stats.listTriangles[list] = values[12 + list];
					}
					stats.softwareTriangles = values[16];
					stats.memory = memory;
				}
			}
		}
	}
}
