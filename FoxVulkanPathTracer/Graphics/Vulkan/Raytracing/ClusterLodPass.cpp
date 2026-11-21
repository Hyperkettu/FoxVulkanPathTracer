#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				PFN_vkGetClusterAccelerationStructureBuildSizesNV ClusterLodPass::vkGetClusterAccelerationStructureBuildSizesNV = nullptr;
				PFN_vkCmdBuildClusterAccelerationStructureIndirectNV ClusterLodPass::vkCmdBuildClusterAccelerationStructureIndirectNV = nullptr;

				namespace {
					const uint32_t GROUP_SIZE = 64; // numthreads in cluster_lod.slang
					const uint32_t BINDINGS = 17;
					const char* SHADERS[4] = { "Shaders/cluster_lod_select.spv", "Shaders/cluster_lod_prepare.spv", "Shaders/cluster_lod_refs.spv", "Shaders/cluster_lod_patch.spv" };

					// Must match Constants in Shaders/cluster_lod.slang
					struct Constants {
						float cameraPosition[3]; // (not glm::vec3: aligned to 16 bytes here)
						float pixelScale;
						uint32_t frameStamp;
						uint32_t workCount;
						uint32_t clusterCount;
						uint32_t instanceCount;
						uint32_t refsAddressLow;
						uint32_t refsAddressHigh;
						uint32_t bottomLevelBudget;
						uint32_t pad1;
					};
					static_assert(sizeof(Constants) == 48, "Constants must match cluster_lod.slang");
					static_assert(sizeof(ClusterLodPass::Instance) == 80, "Instance must match cluster_lod.slang");
					static_assert(sizeof(ClusterLodPass::GpuCluster) == 48, "GpuCluster must match cluster_lod.slang");
					static_assert(sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV) == 64, "The template build info starts with it");
					static_assert(sizeof(VkClusterAccelerationStructureInstantiateClusterInfoNV) == 32, "The instantiate info is copied as 32 bytes");

					uint32_t Groups(uint32_t threads) {
						return (threads + GROUP_SIZE - 1) / GROUP_SIZE;
					}

					VkStridedDeviceAddressRegionKHR Region(VkDeviceAddress address, VkDeviceSize stride, uint32_t count) {
						return { address, stride, stride * count };
					}
				}

				void ClusterLodPass::RegisterExtensionFunctions(VkDevice device) {
					vkGetClusterAccelerationStructureBuildSizesNV = reinterpret_cast<PFN_vkGetClusterAccelerationStructureBuildSizesNV>(
						vkGetDeviceProcAddr(device, "vkGetClusterAccelerationStructureBuildSizesNV"));
					vkCmdBuildClusterAccelerationStructureIndirectNV = reinterpret_cast<PFN_vkCmdBuildClusterAccelerationStructureIndirectNV>(
						vkGetDeviceProcAddr(device, "vkCmdBuildClusterAccelerationStructureIndirectNV"));
				}

				ClusterLodPass::ClusterLodPass(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue)
					: device(device), physicalDevice(physicalDevice), commandPool(commandPool), queue(queue)
				{
					properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_PROPERTIES_NV;
					VkPhysicalDeviceProperties2 properties2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &properties };
					vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

					descriptors = std::make_unique<DescriptorSetBuilder>(device);
					for (uint32_t binding = 0; binding < BINDINGS; binding++) {
						descriptors->AddBinding(binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
					}
					descriptors->SetMaxSets(8).Build();
					layout = std::make_unique<PipelineLayout>(device, std::vector<VkDescriptorSetLayout>{ descriptors->GetLayout().Get() },
						std::vector<VkPushConstantRange>{ { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } });

					for (int pass = 0; pass < 4; pass++) {
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

				ClusterLodPass::~ClusterLodPass() {
					for (VkPipeline pipeline : pipelines) {
						if (pipeline) {
							vkDestroyPipeline(device, pipeline, nullptr);
						}
					}
				}

				std::unique_ptr<Buffer> ClusterLodPass::MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
					size = std::max<VkDeviceSize>(size, 256);
					memory += size;
					return std::make_unique<Buffer>(device, physicalDevice, size, usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						hostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				}

				void ClusterLodPass::UploadTo(Buffer& buffer, const void* data, VkDeviceSize size) {
					if (size == 0) {
						return;
					}
					Buffer staging(device, physicalDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
					std::memcpy(staging.Map(), data, size);
					staging.Unmap();
					CommandList commandList(device, commandPool);
					commandList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT).CopyBuffer(staging.Get(), buffer.Get(), { VkBufferCopy{ 0, 0, size } }).End().SubmitAndWait(queue);
				}

				void ClusterLodPass::Upload(const Content& content, VkBuffer vertices) {
					sceneVertices = vertices;
					clusterCount = static_cast<uint32_t>(content.clusters.size());
					instanceCapacity = std::max(content.instanceCount, 1u);
					memory = 0;
					contentVersion++;
					for (Frame& frame : frames) {
						frame.instances = nullptr; // Resized on the next Record
					}
					if (clusterCount == 0) {
						return;
					}

					const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
					const VkBufferUsageFlags buildInput = storage | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

					// --- What the clusters are ---
					clusters = MakeBuffer(sizeof(GpuCluster) * clusterCount, storage);
					UploadTo(*clusters, content.clusters.data(), sizeof(GpuCluster) * clusterCount);
					localVertexIds = MakeBuffer(sizeof(uint32_t) * content.localVertexIds.size(), storage);
					UploadTo(*localVertexIds, content.localVertexIds.data(), sizeof(uint32_t) * content.localVertexIds.size());
					localIndices = MakeBuffer(content.localIndices.size(), buildInput);
					UploadTo(*localIndices, content.localIndices.data(), content.localIndices.size());
					positions = MakeBuffer(sizeof(float) * 3 * content.localVertexIds.size(), buildInput);
					UploadTo(*positions, content.bindPositions.data(), sizeof(float) * content.bindPositions.size());

					// The limits of every cluster build
					triangleInput = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV };
					triangleInput.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
					triangleInput.maxGeometryIndexValue = std::max(content.maxClustersPerMesh, 1u) - 1;
					triangleInput.maxClusterUniqueGeometryCount = 1;
					triangleInput.maxClusterTriangleCount = content.maxTrianglesPerCluster;
					triangleInput.maxClusterVertexCount = content.maxVerticesPerCluster;
					triangleInput.maxTotalTriangleCount = content.totalTriangles;
					triangleInput.maxTotalVertexCount = static_cast<uint32_t>(content.localVertexIds.size());
					triangleInput.minPositionTruncateBitCount = 0;

					// --- Templates: every cluster's triangles, built once ---
					const VkDeviceAddress indexBase = GetBufferDeviceAddress(device, localIndices->Get());
					const VkDeviceAddress positionBase = GetBufferDeviceAddress(device, positions->Get());
					std::vector<VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV> templateInfos(clusterCount);
					for (uint32_t c = 0; c < clusterCount; c++) {
						std::memcpy(&templateInfos[c], &content.templates[c], sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV));
						templateInfos[c].indexBuffer += indexBase;
						templateInfos[c].vertexBuffer += positionBase;
						templateInfos[c].instantiationBoundingBoxLimit = 0;
					}
					auto templateBuildInfos = MakeBuffer(sizeof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV) * clusterCount, buildInput);
					UploadTo(*templateBuildInfos, templateInfos.data(), sizeof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV) * clusterCount);

					VkClusterAccelerationStructureInputInfoNV templateInput{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
					templateInput.maxAccelerationStructureCount = clusterCount;
					templateInput.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
					templateInput.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV;
					templateInput.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
					templateInput.opInput.pTriangleClusters = &triangleInput;
					VkAccelerationStructureBuildSizesInfoKHR templateSizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
					vkGetClusterAccelerationStructureBuildSizesNV(device, &templateInput, &templateSizes);

					templateData = MakeBuffer(templateSizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
					templateAddresses = MakeBuffer(sizeof(VkDeviceAddress) * clusterCount, storage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
					{
						auto templateScratch = std::make_unique<Buffer>(device, physicalDevice, std::max<VkDeviceSize>(templateSizes.buildScratchSize, 256),
							VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
						auto templateCount = std::make_unique<Buffer>(device, physicalDevice, 256,
							VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
							| VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
						UploadTo(*templateCount, &clusterCount, sizeof(clusterCount));

						VkClusterAccelerationStructureCommandsInfoNV build{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV };
						build.input = templateInput;
						build.dstImplicitData = GetBufferDeviceAddress(device, templateData->Get());
						build.scratchData = GetBufferDeviceAddress(device, templateScratch->Get());
						build.dstAddressesArray = Region(GetBufferDeviceAddress(device, templateAddresses->Get()), sizeof(VkDeviceAddress), clusterCount);
						build.srcInfosArray = Region(GetBufferDeviceAddress(device, templateBuildInfos->Get()),
							sizeof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV), clusterCount);
						build.srcInfosCount = GetBufferDeviceAddress(device, templateCount->Get());
						CommandList commandList(device, commandPool);
						commandList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
						vkCmdBuildClusterAccelerationStructureIndirectNV(commandList.Get(), &build);
						commandList.End().SubmitAndWait(queue);
					}

					// Each frame's instantiation of a cluster: its template, and where its vertices are gathered
					std::vector<VkDeviceAddress> addresses(clusterCount);
					{
						Buffer readback(device, physicalDevice, sizeof(VkDeviceAddress) * clusterCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
							VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
						CommandList commandList(device, commandPool);
						commandList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
							.CopyBuffer(templateAddresses->Get(), readback.Get(), { VkBufferCopy{ 0, 0, sizeof(VkDeviceAddress) * clusterCount } })
							.End().SubmitAndWait(queue);
						std::memcpy(addresses.data(), readback.Map(), sizeof(VkDeviceAddress) * clusterCount);
						readback.Unmap();
					}
					std::vector<VkClusterAccelerationStructureInstantiateClusterInfoNV> instantiations(clusterCount);
					for (uint32_t c = 0; c < clusterCount; c++) {
						auto& info = instantiations[c];
						info = {};
						info.clusterIdOffset = 0;
						info.geometryIndexOffset = 0;
						info.clusterTemplateAddress = addresses[c];
						info.vertexBuffer.startAddress = templateInfos[c].vertexBuffer;
						info.vertexBuffer.strideInBytes = sizeof(float) * 3;
					}
					templates = MakeBuffer(sizeof(VkClusterAccelerationStructureInstantiateClusterInfoNV) * clusterCount, storage);
					UploadTo(*templates, instantiations.data(), sizeof(VkClusterAccelerationStructureInstantiateClusterInfoNV) * clusterCount);

					// --- Every frame's ---
					usedStamp = MakeBuffer(sizeof(uint32_t) * clusterCount, storage);
					{
						const std::vector<uint32_t> zeros(clusterCount, 0u);
						UploadTo(*usedStamp, zeros.data(), sizeof(uint32_t) * clusterCount);
					}
					frameStamp = 0;
					clusterSlot = MakeBuffer(sizeof(uint32_t) * clusterCount, storage);
					counters = MakeBuffer(sizeof(uint32_t) * 4, storage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
					buildInfos = MakeBuffer(sizeof(VkClusterAccelerationStructureInstantiateClusterInfoNV) * clusterCount, buildInput);
					clusterAddresses = MakeBuffer(sizeof(VkDeviceAddress) * clusterCount, storage);

					// Candidate clusters (the levels each instance's distance could use; 12 bytes each): room for every
					// instance's up to this, beyond which RayTracingScene::RecordClusterLod makes them coarser until they fit
					refCapacity = std::max<uint32_t>(content.maxClustersPerMesh * 128, 1u << 21);
					// Clusters actually in the instance structures (about 160 bytes each): a few hundred trees near and far
					// select about 200 000
					bottomLevelBudget = 1u << 19;
					selected = MakeBuffer(sizeof(uint32_t) * refCapacity, storage);
					refs = MakeBuffer(sizeof(VkDeviceAddress) * refCapacity, buildInput);
					instanceCounts = MakeBuffer(sizeof(uint32_t) * instanceCapacity, storage);
					bottomLevelInfos = MakeBuffer(sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV) * instanceCapacity, buildInput);
					bottomLevelAddresses = MakeBuffer(sizeof(VkDeviceAddress) * instanceCapacity, storage);

					// --- The structures' room, for the most there can be ---
					VkClusterAccelerationStructureInputInfoNV clusterInput{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
					clusterInput.maxAccelerationStructureCount = clusterCount;
					clusterInput.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
					clusterInput.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV;
					clusterInput.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
					clusterInput.opInput.pTriangleClusters = &triangleInput;
					VkAccelerationStructureBuildSizesInfoKHR clusterSizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
					vkGetClusterAccelerationStructureBuildSizesNV(device, &clusterInput, &clusterSizes);

					bottomLevelInput = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV };
					bottomLevelInput.maxTotalClusterCount = bottomLevelBudget;
					bottomLevelInput.maxClusterCountPerAccelerationStructure = content.maxClustersPerMesh;
					VkClusterAccelerationStructureInputInfoNV bottomInput{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
					bottomInput.maxAccelerationStructureCount = instanceCapacity;
					bottomInput.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
					bottomInput.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
					bottomInput.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
					bottomInput.opInput.pClustersBottomLevel = &bottomLevelInput;
					VkAccelerationStructureBuildSizesInfoKHR bottomSizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
					vkGetClusterAccelerationStructureBuildSizesNV(device, &bottomInput, &bottomSizes);

					const VkBufferUsageFlags structureStorage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
					clusterData = MakeBuffer(clusterSizes.accelerationStructureSize, structureStorage);
					bottomLevelData = MakeBuffer(bottomSizes.accelerationStructureSize, structureStorage);
					scratch = MakeBuffer(std::max(clusterSizes.buildScratchSize, bottomSizes.buildScratchSize), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

					std::cout << "Cluster LOD: " << clusterCount << " clusters, " << content.totalTriangles << " triangles, " << instanceCapacity
						<< " instances; templates " << templateSizes.accelerationStructureSize / (1024 * 1024) << " MiB, cluster structures "
						<< clusterSizes.accelerationStructureSize / (1024 * 1024) << " MiB, instance structures "
						<< bottomSizes.accelerationStructureSize / (1024 * 1024) << " MiB, all buffers " << memory / (1024 * 1024) << " MiB\n";
				}

				void ClusterLodPass::BindFrameSet(uint32_t frameSlot, VkBuffer tlasInstances) {
					Frame& frame = frames[frameSlot];
					if (!frame.set) {
						frame.set = std::make_unique<DescriptorSet>(descriptors->AllocateSet());
					}
					if (frame.boundTlas == tlasInstances && frame.boundVersion == contentVersion) {
						return;
					}
					const VkBuffer buffers[BINDINGS] = {
						frame.instances->Get(), clusters->Get(), instanceCounts->Get(), selected->Get(), usedStamp->Get(), clusterSlot->Get(),
						counters->Get(), templates->Get(), buildInfos->Get(), localVertexIds->Get(), positions->Get(), sceneVertices,
						clusterAddresses->Get(), refs->Get(), bottomLevelInfos->Get(), bottomLevelAddresses->Get(), tlasInstances,
					};
					VkDescriptorBufferInfo infos[BINDINGS];
					VkWriteDescriptorSet writes[BINDINGS];
					for (uint32_t b = 0; b < BINDINGS; b++) {
						infos[b] = { buffers[b], 0, VK_WHOLE_SIZE };
						writes[b] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
						writes[b].dstSet = frame.set->Get();
						writes[b].dstBinding = b;
						writes[b].descriptorCount = 1;
						writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
						writes[b].pBufferInfo = &infos[b];
					}
					vkUpdateDescriptorSets(device, BINDINGS, writes, 0, nullptr);
					frame.boundTlas = tlasInstances;
					frame.boundVersion = contentVersion;
				}

				void ClusterLodPass::Barrier(VkCommandBuffer cmd) const {
					// Between the passes and the builds: everything written is visible to what comes next
					VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT };
					vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
				}

				void ClusterLodPass::Record(VkCommandBuffer cmd, uint32_t frameSlot, const std::vector<Instance>& instances, uint32_t workCount,
					VkBuffer tlasInstances, const View& view, const std::function<void(const char*)>& mark) {
					if (clusterCount == 0 || instances.empty()) {
						return;
					}
					if (frames.size() <= frameSlot) {
						frames.resize(frameSlot + 1);
					}
					Frame& frame = frames[frameSlot];
					const uint32_t instanceCount = static_cast<uint32_t>(std::min<size_t>(instances.size(), instanceCapacity));
					if (!frame.instances) {
						frame.instances = MakeBuffer(sizeof(Instance) * instanceCapacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
						frame.stats = MakeBuffer(sizeof(uint32_t) * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
						frame.boundVersion = 0;
					}
					std::memcpy(frame.instances->Map(), instances.data(), sizeof(Instance) * instanceCount);
					frame.instances->Unmap();
					frame.instanceCount = instanceCount;
					BindFrameSet(frameSlot, tlasInstances);

					frameStamp++;
					Constants constants{};
					constants.cameraPosition[0] = view.cameraPosition.x;
					constants.cameraPosition[1] = view.cameraPosition.y;
					constants.cameraPosition[2] = view.cameraPosition.z;
					constants.pixelScale = view.pixelScale;
					constants.frameStamp = frameStamp;
					constants.workCount = workCount;
					constants.clusterCount = clusterCount;
					constants.instanceCount = instanceCount;
					const VkDeviceAddress refsAddress = GetBufferDeviceAddress(device, refs->Get());
					constants.refsAddressLow = static_cast<uint32_t>(refsAddress & 0xFFFFFFFFu);
					constants.refsAddressHigh = static_cast<uint32_t>(refsAddress >> 32);
					constants.bottomLevelBudget = bottomLevelBudget;

					// The previous frame's rays (earlier on this queue) are done with the structures this rebuilds
					Barrier(cmd);

					// Counters: nothing built or selected yet; the instance count for the bottom level build
					const uint32_t counterValues[4] = { 0u, instanceCount, 0u, 0u };
					vkCmdUpdateBuffer(cmd, counters->Get(), 0, sizeof(counterValues), counterValues);
					vkCmdFillBuffer(cmd, instanceCounts->Get(), 0, sizeof(uint32_t) * instanceCount, 0u);
					Barrier(cmd);

					const VkDescriptorSet set = frame.set->Get();
					auto dispatch = [&](int pass, uint32_t threads) {
						vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[pass]);
						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout->Get(), 0, 1, &set, 0, nullptr);
						vkCmdPushConstants(cmd, layout->Get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &constants);
						vkCmdDispatch(cmd, Groups(threads), 1, 1);
						Barrier(cmd);
					};

					// 1, 2: select, prepare
					dispatch(0, workCount);
					dispatch(1, clusterCount);
					if (mark) mark("lod select");

					// 3: the clusters used this frame
					VkClusterAccelerationStructureInputInfoNV clusterInput{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
					clusterInput.maxAccelerationStructureCount = clusterCount;
					clusterInput.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
					clusterInput.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV;
					clusterInput.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
					clusterInput.opInput.pTriangleClusters = &triangleInput;
					VkClusterAccelerationStructureCommandsInfoNV clusterBuild{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV };
					clusterBuild.input = clusterInput;
					clusterBuild.dstImplicitData = GetBufferDeviceAddress(device, clusterData->Get());
					clusterBuild.scratchData = GetBufferDeviceAddress(device, scratch->Get());
					clusterBuild.dstAddressesArray = Region(GetBufferDeviceAddress(device, clusterAddresses->Get()), sizeof(VkDeviceAddress), clusterCount);
					clusterBuild.srcInfosArray = Region(GetBufferDeviceAddress(device, buildInfos->Get()), sizeof(VkClusterAccelerationStructureInstantiateClusterInfoNV), clusterCount);
					clusterBuild.srcInfosCount = GetBufferDeviceAddress(device, counters->Get());
					vkCmdBuildClusterAccelerationStructureIndirectNV(cmd, &clusterBuild);
					Barrier(cmd);
					if (mark) mark("clusters");

					// 4: each instance's list
					dispatch(2, workCount);

					// 5: each instance's bottom level structure, from its clusters
					VkClusterAccelerationStructureInputInfoNV bottomInput{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
					bottomInput.maxAccelerationStructureCount = instanceCapacity;
					bottomInput.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
					bottomInput.opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
					bottomInput.opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
					bottomInput.opInput.pClustersBottomLevel = &bottomLevelInput;
					VkClusterAccelerationStructureCommandsInfoNV bottomBuild{ VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV };
					bottomBuild.input = bottomInput;
					bottomBuild.dstImplicitData = GetBufferDeviceAddress(device, bottomLevelData->Get());
					bottomBuild.scratchData = GetBufferDeviceAddress(device, scratch->Get());
					bottomBuild.dstAddressesArray = Region(GetBufferDeviceAddress(device, bottomLevelAddresses->Get()), sizeof(VkDeviceAddress), instanceCapacity);
					bottomBuild.srcInfosArray = Region(GetBufferDeviceAddress(device, bottomLevelInfos->Get()),
						sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV), instanceCapacity);
					bottomBuild.srcInfosCount = GetBufferDeviceAddress(device, counters->Get()) + sizeof(uint32_t);
					vkCmdBuildClusterAccelerationStructureIndirectNV(cmd, &bottomBuild);
					Barrier(cmd);

					// 6: into the top level instances
					dispatch(3, instanceCount);
					if (mark) mark("instances");

					// Statistics, read once this frame slot comes round again
					const VkBufferCopy copy{ 0, 0, sizeof(uint32_t) * 4 };
					vkCmdCopyBuffer(cmd, counters->Get(), frame.stats->Get(), 1, &copy);
					Barrier(cmd);
					frame.statsPending = true;
					frame.instanceCount = instanceCount;
					frame.candidates = workCount;
				}

				void ClusterLodPass::ReadStats(uint32_t frameSlot) {
					if (frameSlot >= frames.size() || !frames[frameSlot].statsPending) {
						return;
					}
					Frame& frame = frames[frameSlot];
					uint32_t values[4];
					std::memcpy(values, frame.stats->Map(), sizeof(values));
					frame.stats->Unmap();
					stats.clustersBuilt = values[0];
					stats.instances = frame.instanceCount;
					stats.clustersSelected = values[2];
					stats.candidates = frame.candidates;
					stats.memory = memory;
					frame.statsPending = false;
				}
			}
		}
	}
}
