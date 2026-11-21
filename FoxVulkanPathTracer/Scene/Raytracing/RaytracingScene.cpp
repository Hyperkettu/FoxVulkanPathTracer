#include "FoxRenderer.h"

namespace Fox {

	namespace Scene {

		namespace RayTracing {

            PFN_vkCreateAccelerationStructureKHR Fox::Scene::RayTracing::RayTracingScene::vkCreateAccelerationStructureKHR;
            PFN_vkDestroyAccelerationStructureKHR Fox::Scene::RayTracing::RayTracingScene::vkDestroyAccelerationStructureKHR;
            PFN_vkCmdBuildAccelerationStructuresKHR Fox::Scene::RayTracing::RayTracingScene::vkCmdBuildAccelerationStructuresKHR;
            PFN_vkGetAccelerationStructureDeviceAddressKHR Fox::Scene::RayTracing::RayTracingScene::vkGetAccelerationStructureDeviceAddressKHR;
            PFN_vkGetAccelerationStructureBuildSizesKHR Fox::Scene::RayTracing::RayTracingScene::vkGetAccelerationStructureBuildSizesKHR;
            bool Fox::Scene::RayTracing::RayTracingScene::opacityMicromapsEnabled = false;
            bool Fox::Scene::RayTracing::RayTracingScene::clusterLodEnabled = false;

            void CreateScratchBuffer(
                VkDevice device,
                VkPhysicalDevice physicalDevice,
                VkDeviceSize size,
                VkBuffer& buffer,
                VkDeviceMemory& memory,
                const std::string& name = "")
            {
                VkBufferCreateInfo bufferInfo{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                    .size = size,
                    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE
                };

                vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);

                VkMemoryRequirements memReq;
                vkGetBufferMemoryRequirements(device, buffer, &memReq);

                VkMemoryAllocateFlagsInfo flags{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
                    .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
                };

                VkMemoryAllocateInfo alloc{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    .pNext = &flags,
                    .allocationSize = memReq.size,
                    .memoryTypeIndex = Fox::Scene::RayTracing::RayTracingScene::FindDeviceLocalMemoryType(
                        physicalDevice, memReq.memoryTypeBits)
                };

                vkAllocateMemory(device, &alloc, nullptr, &memory);

                vkBindBufferMemory(device, buffer, memory, 0);

             

#ifdef _DEBUG
                if (name.size() > 0) {
                    Fox::Graphics::Vulkan::CommandList::SetName(name, reinterpret_cast<uint64_t>(buffer), VkObjectType::VK_OBJECT_TYPE_BUFFER, device);
                    Fox::Graphics::Vulkan::CommandList::PrintBufferNameAndAddress(device, buffer, name);
                }
#endif
            }

            RayTracingScene::RayTracingScene(
                VkDevice device,
                VkPhysicalDevice physicalDevice,
                VkQueue queue,
                uint32_t queueFamilyIndex)
                : device(device)
                , physicalDevice(physicalDevice)
                , queue(queue)
                , queueFamily(queueFamilyIndex)
            {
                commandPool = std::make_unique<Fox::Graphics::Vulkan::CommandPool>(device, queueFamilyIndex);
            }

            RayTracingScene::~RayTracingScene()
            {
                ReleaseGPUResources();
            }

            void RayTracingScene::Clear() {
                ReleaseGPUResources();

                vertices.clear();
                indices.clear();
                submeshes.clear();
                materials.clear();
                lights.clear();
                lightCount = 0;
                clutterInstances = 0;
                uploadedMotion.clear();
                meshRecords.clear();
                placements.clear();
                instances.clear();
                instanceBlasIndices.clear();
                rasterVersion++;
                instancePlacements.clear();
                skinnedMeshes.clear();
                bindPose.clear();
                jointCount = 0;
                vertexOffset = 0;
                indexOffset = 0;
                materialOffset = 0;
                opacityGeometries.clear();
                editableSurfaces.clear();
                dirtyMaterials.clear();
                staticCounts = StaticCounts{};
                tlasDirty = false;
                lightsDirty = false;
            }

            void RayTracingScene::ReleaseGPUResources()
            {
                // Micromaps after the structures that use them
                skinningPass = nullptr;

                // Destroy TLAS
                if (topLevelAccelerationStructure.handle != VK_NULL_HANDLE) {
                    vkDestroyAccelerationStructureKHR(
                        device,
                        topLevelAccelerationStructure.handle,
                        nullptr);
                }

                if (topLevelAccelerationStructure.buffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, topLevelAccelerationStructure.buffer, nullptr);
                }

                if (topLevelAccelerationStructure.memory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, topLevelAccelerationStructure.memory, nullptr);
                }

                if (dynamicScratchBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, dynamicScratchBuffer, nullptr);
                    vkFreeMemory(device, dynamicScratchMemory, nullptr);
                }
                if (editScratchBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, editScratchBuffer, nullptr);
                    vkFreeMemory(device, editScratchMemory, nullptr);
                    editScratchBuffer = VK_NULL_HANDLE;
                    editScratchMemory = VK_NULL_HANDLE;
                    editScratchSize = 0;
                }

                // Destroy BLASes
                for (auto& blas : bottomLevelAccelerationStructure)
                {
                    if (blas.handle != VK_NULL_HANDLE) {
                        vkDestroyAccelerationStructureKHR(device, blas.handle, nullptr);
                    }

                    if (blas.buffer != VK_NULL_HANDLE) {
                        vkDestroyBuffer(device, blas.buffer, nullptr);
                    }

                    if (blas.memory != VK_NULL_HANDLE) {
                        vkFreeMemory(device, blas.memory, nullptr);
                    }
                }

                for (auto scratchBuffer : scratchBuffers) {
                    vkDestroyBuffer(device, scratchBuffer, nullptr);
                }

                for (auto scratchBufferMemory : scratchBufferMemories) {
                    vkFreeMemory(device, scratchBufferMemory, nullptr);
                }

                topLevelAccelerationStructure = TopLevelAccelerationStructure{};
                bottomLevelAccelerationStructure.clear();
                blasRanges.clear();
                rasterVersion++;
                scratchBuffers.clear();
                scratchBufferMemories.clear();
                scratchBuffer = VK_NULL_HANDLE;
                scratchMemory = VK_NULL_HANDLE;
                dynamicScratchBuffer = VK_NULL_HANDLE;
                dynamicScratchMemory = VK_NULL_HANDLE;
                dynamicScratchSize = 0;
                tlasScratchAddress = 0;
                blasScratchAddress = 0;

                staticMicromaps = OpacityMicromapSet{};
                dynamicMicromaps = OpacityMicromapSet{};

                instanceBuffer = nullptr;
                frameInstanceBuffers.clear();
                vertexBuffers.clear();
                indexBuffers.clear();
                vertexSSBO = nullptr;
                indexSSBO = nullptr;
                submeshSSBO = nullptr;
                materialsSSBO = nullptr;
                lightsSSBO = nullptr;
                instanceMotionSSBO = nullptr;
                previousPositionsSSBO = nullptr;
                bindPoseSSBO = nullptr;
                jointsSSBO = nullptr;
            }
		
            uint32_t RayTracingScene::AddBLAS(
                VkDeviceAddress vertexAddress,
                VkDeviceAddress indexAddress,
                uint32_t firstSubmesh,
                uint32_t submeshCount,
                bool dynamic)
            {
                BottomLevelAccelerationStructure blas{};
                blas.dynamic = dynamic;
                blasRanges.resize(bottomLevelAccelerationStructure.size());
                blasRanges.push_back({ firstSubmesh, submeshCount, dynamic });
                rasterVersion++;

                for (uint32_t i = firstSubmesh; i < firstSubmesh + submeshCount; i++) {
                    const Fox::Graphics::Vulkan::Submesh& submesh = submeshes[i];

                    VkAccelerationStructureGeometryTrianglesDataKHR triangles{
                        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                        .vertexData = { vertexAddress },
                        .vertexStride = sizeof(Fox::Graphics::Vulkan::Vertex),
                        .maxVertex = submesh.vertexOffset + std::max(submesh.vertexCount, 1u) - 1,
                        .indexType = VK_INDEX_TYPE_UINT32,
                        .indexData = { indexAddress }
                    };

                    // Alpha tested surfaces are not opaque: rays run the any-hit shader on them, except where an
                    // opacity micromap already knows the answer
                    const bool alphaTested = materials[submesh.materialIndex].IsAlphaTested();
                    if (auto opacity = opacityGeometries.find(i); opacity != opacityGeometries.end()) {
                        triangles.pNext = &opacity->second->info;
                    }

                    VkAccelerationStructureGeometryKHR geometry{
                        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
                        .flags = alphaTested ? VkGeometryFlagsKHR(0) : VkGeometryFlagsKHR(VK_GEOMETRY_OPAQUE_BIT_KHR)
                    };
                    geometry.geometry.triangles = triangles;

                    // Indices are scene-wide (already offset by the submesh's first vertex)
                    VkAccelerationStructureBuildRangeInfoKHR range{
                        .primitiveCount = submesh.indexCount / 3,
                        .primitiveOffset = submesh.indexOffset * static_cast<uint32_t>(sizeof(uint32_t)),
                        .firstVertex = 0,
                        .transformOffset = 0
                    };

                    blas.geometries.push_back(geometry);
                    blas.ranges.push_back(range);
                    blas.primitiveCounts.push_back(range.primitiveCount);
                }

                bottomLevelAccelerationStructure.push_back(blas);
                return uint32_t(bottomLevelAccelerationStructure.size() - 1);
            }


            void RayTracingScene::AddInstance(
                uint32_t blasIndex,
                const glm::mat4& transform,
                uint32_t instanceCustomIndex,
                uint32_t mask)
            {
                VkAccelerationStructureInstanceKHR instance{};
                instance.transform = {
                    transform[0][0], transform[1][0], transform[2][0], transform[3][0],
                    transform[0][1], transform[1][1], transform[2][1], transform[3][1],
                    transform[0][2], transform[1][2], transform[2][2], transform[3][2]
                };

                instance.instanceCustomIndex = instanceCustomIndex;
                instance.mask = mask;
                instance.instanceShaderBindingTableRecordOffset = 0;
                instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                instance.accelerationStructureReference = 0;

                instances.push_back(instance);
                instanceBlasIndices.push_back(blasIndex);
                rasterVersion++;
            }

            namespace {
                // Deforming geometry is refitted every frame instead of rebuilt
                VkBuildAccelerationStructureFlagsKHR BlasBuildFlags(const BottomLevelAccelerationStructure& blas) {
                    return VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                        | (blas.dynamic ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0);
                }
            }

            void RayTracingScene::BuildBLAS(VkCommandBuffer cmd)
            {
                if (bottomLevelAccelerationStructure.empty()) {
                    return; // An empty scene, or only dynamic meshes (built later)
                }

                VkDeviceSize maxScratchSize = 0;

                for (auto& blas : bottomLevelAccelerationStructure)
                {
                    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
                        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                        .flags = BlasBuildFlags(blas),
                        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
                        .geometryCount = uint32_t(blas.geometries.size()),
                        .pGeometries = blas.geometries.data()
                    };

                    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
                        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
                    };

                    vkGetAccelerationStructureBuildSizesKHR(
                        device,
                        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                        &buildInfo,
                        blas.primitiveCounts.data(), 
                        &sizeInfo);
                    blas.sizeInfo = sizeInfo;

                    // The same scratch memory later refits the deforming ones
                    maxScratchSize = std::max({ maxScratchSize, sizeInfo.buildScratchSize, blas.dynamic ? sizeInfo.updateScratchSize : VkDeviceSize(0) });

                    CreateAccelerationStructure(
                        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                        sizeInfo.accelerationStructureSize,
                        blas.handle,
                        blas.buffer,
                        blas.memory);
                }

                VkBuffer globalScratchBuffer;
                VkDeviceMemory globalScratchMemory;
                CreateScratchBuffer(device, physicalDevice, maxScratchSize, globalScratchBuffer, globalScratchMemory, "Global BLAS Scratch");
                VkDeviceAddress scratchAddress = GetBufferAddress(globalScratchBuffer);
                blasScratchAddress = scratchAddress;

                scratchBuffers.push_back(globalScratchBuffer);
                scratchBufferMemories.push_back(globalScratchMemory); 

                for (auto& blas : bottomLevelAccelerationStructure)
                {
                    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
                        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                        .flags = BlasBuildFlags(blas),
                        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
                        .geometryCount = uint32_t(blas.geometries.size()),
                        .pGeometries = blas.geometries.data(),
                        .scratchData = {.deviceAddress = scratchAddress }
                    };

                    buildInfo.dstAccelerationStructure = blas.handle;


                    const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { blas.ranges.data() };

                    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, ranges);

                    VkMemoryBarrier barrier{
                        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                        .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                        .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
                    };
                    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        0, 1, &barrier, 0, nullptr, 0, nullptr);

                    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{
                        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
                        .accelerationStructure = blas.handle
                    };
                    blas.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &addrInfo);
                }
            }

            void RayTracingScene::AddInstance(
                uint32_t blasIndex,
                const VkTransformMatrixKHR& transform,
                uint32_t instanceCustomIndex,
                uint32_t mask)
            {
                VkAccelerationStructureInstanceKHR instance{};
                instance.transform = transform;

                instance.instanceCustomIndex = instanceCustomIndex;
                instance.mask = mask;
                instance.instanceShaderBindingTableRecordOffset = 0;
                instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                instance.accelerationStructureReference = 0;

                instances.push_back(instance);
                instanceBlasIndices.push_back(blasIndex);
                rasterVersion++;
            }

            void RayTracingScene::BuildTLAS(VkCommandBuffer cmd)
            {
                VkMemoryBarrier barrier{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                    .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
                };

                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    0, 1, &barrier, 0, nullptr, 0, nullptr);

                for (size_t i = 0; i < instances.size(); ++i) {
                    instances[i].accelerationStructureReference = instanceBlasIndices[i] == NO_BLAS ? 0 : bottomLevelAccelerationStructure[instanceBlasIndices[i]].deviceAddress;
                }

                UploadInstanceBuffer();

                VkDeviceAddress instanceAddress =
                    GetBufferAddress(instanceBuffer->Get());

                VkAccelerationStructureGeometryInstancesDataKHR instances{
                    .sType =
                        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                    .arrayOfPointers = VK_FALSE,
                    .data = { instanceAddress }
                };

                VkAccelerationStructureGeometryKHR geometry{
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                    .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
                    .geometry = {.instances = instances }
                };

                VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                    .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
                    .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
                    .geometryCount = 1,
                    .pGeometries = &geometry
                };

                uint32_t instanceCount = uint32_t(this->instances.size());

                VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
                };

                // Sized for the most instances there can be: dynamic meshes come and go, the structure stays
                const uint32_t maxInstances = MAX_INSTANCES;
                vkGetAccelerationStructureBuildSizesKHR(
                    device,
                    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                    &buildInfo,
                    &maxInstances,
                    &sizeInfo);

                CreateAccelerationStructure(
                    VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
                    sizeInfo.accelerationStructureSize,
                    topLevelAccelerationStructure.handle,
                    topLevelAccelerationStructure.buffer,
                    topLevelAccelerationStructure.memory);

                CreateScratchBuffer( 
                    device, 
                    physicalDevice, 
                    sizeInfo.buildScratchSize,
                    scratchBuffer, 
                    scratchMemory);

                scratchBuffers.push_back(scratchBuffer); 
                scratchBufferMemories.push_back(scratchMemory);

                buildInfo.dstAccelerationStructure = topLevelAccelerationStructure.handle;
                buildInfo.scratchData.deviceAddress =
                    GetBufferAddress(scratchBuffer);
                tlasScratchAddress = buildInfo.scratchData.deviceAddress; // Reused when objects move

                VkAccelerationStructureBuildRangeInfoKHR range{
                    .primitiveCount = instanceCount
                };

                const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { &range };

                vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, ranges);

                VkAccelerationStructureDeviceAddressInfoKHR addrInfo{
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
                    .accelerationStructure = topLevelAccelerationStructure.handle
                };
                topLevelAccelerationStructure.deviceAddress =
                    vkGetAccelerationStructureDeviceAddressKHR(device, &addrInfo);
            }

            void RayTracingScene::CreateAccelerationStructure(
                VkAccelerationStructureTypeKHR type,
                VkDeviceSize size,
                VkAccelerationStructureKHR& accel,
                VkBuffer& buffer,
                VkDeviceMemory& memory)
            {
                VkBufferCreateInfo bufferInfo{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                    .size = size,
                    .usage =
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE
                };

                vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);

                // 2. Get memory requirements
                VkMemoryRequirements memReq;
                vkGetBufferMemoryRequirements(device, buffer, &memReq);

                // 3. Allocate memory with device address
                VkMemoryAllocateFlagsInfo flagsInfo{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
                    .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
                };

                VkMemoryAllocateInfo allocInfo{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    .pNext = &flagsInfo,
                    .allocationSize = memReq.size,
                    .memoryTypeIndex = Fox::Scene::RayTracing::RayTracingScene::FindDeviceLocalMemoryType(
                        physicalDevice, memReq.memoryTypeBits)
                };

                vkAllocateMemory(device, &allocInfo, nullptr, &memory);
                vkBindBufferMemory(device, buffer, memory, 0);

                // 4. Create acceleration structure
                VkAccelerationStructureCreateInfoKHR asInfo{
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
                    .buffer = buffer,
                    .offset = 0,
                    .size = size,
                    .type = type
                };

                vkCreateAccelerationStructureKHR(
                    device, &asInfo, nullptr, &accel);
            }


            void RayTracingScene::UploadInstanceBuffer()
            {
                // At least one entry: an empty scene still has a (empty) top level structure
                VkDeviceSize size =
                    sizeof(VkAccelerationStructureInstanceKHR) * std::max<size_t>(instances.size(), 1);

                instanceBuffer = std::make_unique<Fox::Graphics::Vulkan::Buffer>(
                    device,
                    physicalDevice,
                    size,
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                void* mapped = instanceBuffer->Map();
                if (!instances.empty()) {
                    memcpy(mapped, instances.data(), sizeof(VkAccelerationStructureInstanceKHR) * instances.size());
                }
                instanceBuffer->Unmap();
            }

            uint32_t RayTracingScene::FindDeviceLocalMemoryType(
                VkPhysicalDevice physicalDevice,
                uint32_t typeFilter)
            {
                VkPhysicalDeviceMemoryProperties memProps;
                vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

                for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
                    if ((typeFilter & (1 << i)) &&
                        (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                        return i;
                    }
                }

                throw std::runtime_error("Failed to find DEVICE_LOCAL memory type!");
            }


            VkDeviceAddress RayTracingScene::GetBufferAddress(VkBuffer buffer)
            {
                VkBufferDeviceAddressInfo info{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                    .buffer = buffer
                };
                return vkGetBufferDeviceAddress(device, &info);
            }

            size_t RayTracingScene::AddMeshGeometry(const Fox::Graphics::Geometry::Vulkan::Mesh* mesh) {
                const auto& meshVertices = mesh->GetVertices();
                const auto& meshIndices = mesh->GetIndices();
                const auto& meshMaterials = mesh->GetMaterials();
                const auto& meshSubmeshes = mesh->GetSubmeshesForGPU();
                const auto& meshInstances = mesh->GetInstanceData(); // One per submesh: its node transform in the file

                vertices.insert(vertices.end(), meshVertices.begin(), meshVertices.end());

                // Indices are relative to the mesh, rebase them onto the scene-wide vertex array
                indices.reserve(indices.size() + meshIndices.size());
                for (uint32_t index : meshIndices) {
                    indices.push_back(index + vertexOffset);
                }

                MeshRecord record;
                record.mesh = mesh;
                record.firstSubmesh = static_cast<uint32_t>(submeshes.size());

                // Bake each submesh's node transform into its vertices, so the whole mesh is one acceleration
                // structure in mesh space. Many overlapping per-submesh instances would be far slower to traverse.
                std::vector<Fox::Graphics::Vulkan::Submesh> rebased;
                for (size_t i = 0; i < meshSubmeshes.size(); i++) {
                    Fox::Graphics::Vulkan::Submesh submesh = meshSubmeshes[i];
                    submesh.vertexOffset += vertexOffset;
                    submesh.indexOffset += indexOffset;
                    submesh.materialIndex += materialOffset;

                    if (i < meshInstances.size()) {
                        TransformVertices(submesh.vertexOffset, submesh.vertexCount, meshInstances[i].transform);
                    }
                    rebased.push_back(submesh);
                }

                // Grouped by kind (opaque, thin glass, solid glass): each group becomes one BLAS
                for (int kind = 0; kind < SurfaceKindCount; kind++) {
                    for (const auto& submesh : rebased) {
                        if (KindOf(meshMaterials[submesh.materialIndex - materialOffset]) == kind) {
                            submeshes.push_back(submesh);
                            record.count[kind]++;
                        }
                    }
                }

                // Materials refer to the lights of the mesh's first placement (added right after this)
                const int lightOffset = static_cast<int>(lights.size());
                for (Fox::Graphics::Vulkan::Material material : meshMaterials) {
                    if (material.lightIndex >= 0) {
                        material.lightIndex += lightOffset;
                    }
                    materials.push_back(material);
                }

                // Emissive triangles: lights after the mesh's own
                record.firstLight = static_cast<uint32_t>(lightOffset);
                record.triangleLights = MakeTriangleLights(record.firstSubmesh, static_cast<uint32_t>(submeshes.size()) - record.firstSubmesh,
                    static_cast<uint32_t>(lightOffset + mesh->GetLights().size()));

                vertexOffset += static_cast<uint32_t>(meshVertices.size());
                indexOffset += static_cast<uint32_t>(meshIndices.size());
                materialOffset += static_cast<uint32_t>(meshMaterials.size());

                meshRecords.push_back(record);
                return meshRecords.size() - 1;
            }

            size_t RayTracingScene::AddInstancedPlacement(size_t record, std::vector<glm::mat4> items, const std::string& name) {
                // Within the budget: a copy takes a top level instance per surface kind of the mesh
                const MeshRecord& meshRecord = meshRecords[record];
                uint32_t kinds = 0;
                for (int kind = 0; kind < SurfaceKindCount; kind++) {
                    kinds += meshRecord.count[kind] > 0 ? 1 : 0;
                }
                kinds = std::max(kinds, 1u);
                const size_t room = (MAX_CLUTTER_INSTANCES - std::min(clutterInstances, MAX_CLUTTER_INSTANCES)) / kinds;
                if (items.size() > room) {
                    items.resize(room);
                }
                clutterInstances += static_cast<uint32_t>(items.size()) * kinds;

                Placement placement;
                placement.record = record;
                placement.name = name;
                placement.movable = true;
                placement.firstLight = meshRecord.firstLight; // Its lights are not sampled (see RecordFrameUpdate)
                placement.items = std::move(items);
                placements.push_back(std::move(placement));
                return placements.size() - 1;
            }

            size_t RayTracingScene::AddInstancedMesh(const Fox::Graphics::Geometry::Vulkan::Mesh& mesh, std::vector<glm::mat4> items, const std::string& name) {
                auto existing = std::find_if(meshRecords.begin(), meshRecords.end(),
                    [&mesh](const MeshRecord& record) { return record.mesh == &mesh; });
                const size_t record = existing != meshRecords.end()
                    ? static_cast<size_t>(existing - meshRecords.begin())
                    : AddMeshGeometry(&mesh);
                return AddInstancedPlacement(record, std::move(items), name);
            }

            size_t RayTracingScene::AddInstancedSurface(const std::vector<Fox::Graphics::Vulkan::Vertex>& surfaceVertices,
                const std::vector<uint32_t>& surfaceIndices, const Fox::Graphics::Vulkan::Material& material, std::vector<glm::mat4> items,
                const std::string& name) {
                vertices.insert(vertices.end(), surfaceVertices.begin(), surfaceVertices.end());
                for (uint32_t index : surfaceIndices) {
                    indices.push_back(vertexOffset + index);
                }

                Fox::Graphics::Vulkan::Submesh submesh{};
                submesh.vertexOffset = vertexOffset;
                submesh.indexOffset = indexOffset;
                submesh.vertexCount = static_cast<uint32_t>(surfaceVertices.size());
                submesh.indexCount = static_cast<uint32_t>(surfaceIndices.size());
                submesh.materialIndex = materialOffset;

                MeshRecord record;
                record.firstSubmesh = static_cast<uint32_t>(submeshes.size());
                record.count[KindOf(material)] = 1;
                record.firstLight = static_cast<uint32_t>(lights.size());
                meshRecords.push_back(record);
                submeshes.push_back(submesh);

                Fox::Graphics::Vulkan::Material surfaceMaterial = material;
                surfaceMaterial.lightIndex = -1;
                materials.push_back(surfaceMaterial);

                vertexOffset += submesh.vertexCount;
                indexOffset += submesh.indexCount;
                materialOffset += 1;
                return AddInstancedPlacement(meshRecords.size() - 1, std::move(items), name);
            }

            void RayTracingScene::AddMesh(const Fox::Graphics::Geometry::Vulkan::Mesh& mesh, const glm::mat4& transform, const std::string& name) {
                // Geometry is stored once per mesh and shared by all its placements
                auto existing = std::find_if(meshRecords.begin(), meshRecords.end(),
                    [&mesh](const MeshRecord& record) { return record.mesh == &mesh; });

                const size_t record = existing != meshRecords.end()
                    ? static_cast<size_t>(existing - meshRecords.begin())
                    : AddMeshGeometry(&mesh);

                Placement placement;
                placement.record = record;
                placement.transform = Decompose(transform);
                placement.name = name;

                // Lights are stored in world space; the mesh-space originals follow the object when it moves
                placement.firstLight = static_cast<uint32_t>(lights.size());
                placement.localLights = mesh.GetLights();
                const auto& triangleLights = meshRecords[record].triangleLights;
                placement.localLights.insert(placement.localLights.end(), triangleLights.begin(), triangleLights.end());
                for (const Fox::Graphics::Vulkan::Light& light : placement.localLights) {
                    lights.push_back(TransformLight(light, transform));
                }

                placements.push_back(std::move(placement));
            }

            std::vector<Fox::Graphics::Vulkan::Light> RayTracingScene::MakeTriangleLights(uint32_t first, uint32_t count, uint32_t firstLightIndex) {
                std::vector<Fox::Graphics::Vulkan::Light> result;
                for (uint32_t s = first; s < first + count && s < submeshes.size(); s++) {
                    Fox::Graphics::Vulkan::Submesh& submesh = submeshes[s];
                    const Fox::Graphics::Vulkan::Material& material = materials[submesh.materialIndex];
                    const float emission = std::max({ material.emissive[0], material.emissive[1], material.emissive[2] });
                    // Not: dark, terrain and water (their emissive fields mean other things), quads already lights
                    if (emission <= 0.0f || material.layerMaterial != -1 || material.lightIndex >= 0 || submesh.pad1 != 0) {
                        continue;
                    }
                    const uint32_t triangles = submesh.indexCount / 3;
                    if (result.size() + triangles > MAX_TRIANGLE_LIGHTS) {
                        std::cout << "Emissive surface with too many triangles to sample as lights (" << triangles << "): lit only when hit\n";
                        continue;
                    }

                    submesh.pad2 = firstLightIndex + static_cast<uint32_t>(result.size()) + 1;
                    for (uint32_t t = 0; t < triangles; t++) {
                        const Fox::Graphics::Vulkan::Vertex* corner[3];
                        for (int c = 0; c < 3; c++) {
                            corner[c] = &vertices[indices[submesh.indexOffset + 3 * t + c]];
                        }
                        auto position = [](const Fox::Graphics::Vulkan::Vertex* v) { return glm::vec3(v->position[0], v->position[1], v->position[2]); };

                        Fox::Graphics::Vulkan::Light light{};
                        light.type = static_cast<int>(Fox::Core::Loaders::GLTF::LightType::EmissiveTriangle);
                        light.position = position(corner[0]);
                        light.tangentX = position(corner[1]) - light.position;
                        light.tangentY = position(corner[2]) - light.position;
                        light.color = glm::vec3(material.emissive[0], material.emissive[1], material.emissive[2]);
                        light.material = static_cast<int>(submesh.materialIndex);
                        light.u0 = corner[0]->uv[0]; light.v0 = corner[0]->uv[1];
                        light.u1 = corner[1]->uv[0]; light.v1 = corner[1]->uv[1];
                        light.u2 = corner[2]->uv[0]; light.v2 = corner[2]->uv[1];
                        result.push_back(TransformLight(light, glm::mat4(1.0f))); // Normal and area
                    }
                }
                return result;
            }

            void RayTracingScene::BuildLightSelection() {
                using Fox::Core::Loaders::GLTF::LightType;
                auto isArea = [](const Fox::Graphics::Vulkan::Light& light) {
                    return light.type == static_cast<int>(LightType::QuadAreaLight) || light.type == static_cast<int>(LightType::EmissiveTriangle);
                };
                std::vector<double> weights(lightCount, 0.0);
                double analytic = 0.0, area = 0.0;
                for (uint32_t i = 0; i < lightCount; i++) {
                    const auto& light = lights[i];
                    const double luminance = std::max(0.0, 0.2126 * light.color.r + 0.7152 * light.color.g + 0.0722 * light.color.b);
                    if (isArea(light)) {
                        weights[i] = luminance * std::max(light.area, 0.0f);
                        area += weights[i];
                    }
                    else {
                        weights[i] = luminance > 0.0 ? 1.0 : 0.0; // Off lamps are not sampled
                        analytic += weights[i];
                    }
                }
                const double areaShare = area > 0.0 ? (analytic > 0.0 ? 0.5 : 1.0) : 0.0;
                double sum = 0.0;
                uint32_t last = 0;
                for (uint32_t i = 0; i < lightCount; i++) {
                    auto& light = lights[i];
                    const double pool = isArea(light) ? area : analytic;
                    const double share = isArea(light) ? areaShare : 1.0 - areaShare;
                    const double selection = pool > 0.0 ? share * weights[i] / pool : 0.0;
                    sum += selection;
                    light.selection = static_cast<float>(selection);
                    light.cumulative = static_cast<float>(sum);
                    if (selection > 0.0) {
                        last = i;
                    }
                }
                // Rounding: the last light that can be picked ends the range
                for (uint32_t i = last; i < lightCount; i++) {
                    lights[i].cumulative = 1.0f;
                }
            }

            Fox::Graphics::Vulkan::Light RayTracingScene::TransformLight(Fox::Graphics::Vulkan::Light light, const glm::mat4& transform) {
                const glm::mat3 linearTransform(transform);
                if (light.type == (int)Fox::Core::Loaders::GLTF::LightType::EmissiveTriangle) {
                    light.position = glm::vec3(transform * glm::vec4(light.position, 1.0f));
                    light.tangentX = linearTransform * light.tangentX;
                    light.tangentY = linearTransform * light.tangentY;
                    const glm::vec3 normal = glm::cross(light.tangentX, light.tangentY);
                    const float length = glm::length(normal);
                    light.area = 0.5f * length;
                    light.normal = length > 0.0f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
                    return light;
                }
                if (light.type == (int)Fox::Core::Loaders::GLTF::LightType::QuadAreaLight) {
                    light.position = glm::vec3(transform * glm::vec4(light.position, 1.0f));
                    light.tangentX = linearTransform * light.tangentX;
                    light.tangentY = linearTransform * light.tangentY;
                    // The tangents are half extents
                    light.area = glm::length(glm::cross(2.0f * light.tangentX, 2.0f * light.tangentY));
                }

                light.normal = glm::normalize(glm::transpose(glm::inverse(linearTransform)) * light.normal);
                return light;
            }

            RayTracingScene::ObjectTransform RayTracingScene::Decompose(const glm::mat4& matrix) {
                // Translation, rotation and scale; shear is not supported
                ObjectTransform result;
                result.position = glm::vec3(matrix[3]);

                glm::mat3 linear(matrix);
                for (int axis = 0; axis < 3; axis++) {
                    result.scale[axis] = glm::length(linear[axis]);
                    if (result.scale[axis] > 0.0f) {
                        linear[axis] /= result.scale[axis];
                    }
                }

                // A mirroring transform: keep the rotation proper, put the flip into the scale
                if (glm::determinant(linear) < 0.0f) {
                    result.scale.x = -result.scale.x;
                    linear[0] = -linear[0];
                }

                result.rotation = glm::normalize(glm::quat_cast(linear));
                return result;
            }

            size_t RayTracingScene::AddSkinnedMesh(const Fox::Graphics::Geometry::SkinnedMesh& mesh, const std::vector<Fox::Graphics::Vulkan::Material>& meshMaterials,
                const glm::mat4& transform, const std::string& name, const Fox::Graphics::Geometry::ClusterLod* lod) {
                SkinnedRecord skinned;
                skinned.firstVertex = vertexOffset;
                skinned.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
                skinned.firstSkinVertex = static_cast<uint32_t>(bindPose.size());
                skinned.firstJoint = jointCount;

                // The bind pose is the rest pose: identity skin matrices
                JointMatrix identity;
                identity.rows[0] = glm::vec4(1, 0, 0, 0);
                identity.rows[1] = glm::vec4(0, 1, 0, 0);
                identity.rows[2] = glm::vec4(0, 0, 1, 0);
                skinned.joints.assign(std::max<size_t>(mesh.bones.size(), 1), identity);
                jointCount += static_cast<uint32_t>(skinned.joints.size());

                for (size_t i = 0; i < mesh.vertices.size(); i++) {
                    const Fox::Graphics::Vulkan::Vertex& vertex = mesh.vertices[i];
                    SkinVertex skinVertex{};
                    std::memcpy(skinVertex.position, vertex.position, sizeof(skinVertex.position));
                    std::memcpy(skinVertex.normal, vertex.normal, sizeof(skinVertex.normal));
                    std::memcpy(skinVertex.tangent, vertex.tangent, sizeof(skinVertex.tangent));
                    if (i < mesh.skin.size()) {
                        std::memcpy(skinVertex.joints, mesh.skin[i].joints, sizeof(skinVertex.joints));
                        std::memcpy(skinVertex.weights, mesh.skin[i].weights, sizeof(skinVertex.weights));
                    }
                    else {
                        skinVertex.weights[0] = 1.0f;
                    }
                    if (i < mesh.growth.size()) {
                        std::memcpy(skinVertex.growth, &mesh.growth[i].x, sizeof(skinVertex.growth));
                    }
                    bindPose.push_back(skinVertex);
                }

                // One submesh per part, all over the mesh's vertex range (the shaders index vertices scene-wide)
                std::vector<Fox::Graphics::Geometry::SkinnedMesh::Part> parts = mesh.parts;
                if (parts.empty()) {
                    parts.push_back({ 0, static_cast<uint32_t>(mesh.indices.size()), 0 });
                }

                MeshRecord record;
                record.firstSubmesh = static_cast<uint32_t>(submeshes.size());
                record.dynamic = true;
                const bool clustered = lod && clusterLodEnabled && !lod->clusters.empty();
                if (clustered) {
                    // One submesh per cluster (the shaders find a hit's triangle as for any submesh: the instance's
                    // custom index is the first, the geometry index the cluster), the levels' triangles as its indices
                    ClusterMesh clusterMesh;
                    clusterMesh.firstCluster = static_cast<uint32_t>(clusterContent.clusters.size());
                    clusterMesh.clusterCount = static_cast<uint32_t>(lod->clusters.size());
                    record.clusterMesh = static_cast<int>(clusterMeshes.size());

                    const uint32_t localVertexBase = static_cast<uint32_t>(clusterContent.localVertexIds.size());
                    const uint32_t localIndexBase = static_cast<uint32_t>(clusterContent.localIndices.size());
                    for (uint32_t v : lod->localVertices) {
                        clusterContent.localVertexIds.push_back(vertexOffset + v);
                        clusterContent.bindPositions.insert(clusterContent.bindPositions.end(), mesh.vertices[v].position, mesh.vertices[v].position + 3);
                    }
                    clusterContent.localIndices.insert(clusterContent.localIndices.end(), lod->localIndices.begin(), lod->localIndices.end());

                    glm::vec3 center(0.0f);
                    for (const auto& cluster : lod->clusters) {
                        center += glm::vec3(cluster.selfBounds);
                    }
                    center /= static_cast<float>(lod->clusters.size());
                    float radius = 0.0f;
                    for (uint32_t j = 0; j < lod->clusters.size(); j++) {
                        const auto& cluster = lod->clusters[j];
                        const Fox::Graphics::Vulkan::Material& material = meshMaterials.at(parts.at(cluster.part).material);
                        if (KindOf(material) != Opaque) {
                            throw std::runtime_error("Skinned meshes support opaque and alpha tested materials only");
                        }

                        Fox::Graphics::Vulkan::Submesh submesh{};
                        submesh.vertexOffset = vertexOffset;
                        submesh.vertexCount = skinned.vertexCount;
                        submesh.indexOffset = indexOffset + cluster.firstIndex;
                        submesh.indexCount = cluster.triangleCount * 3;
                        submesh.materialIndex = materialOffset + parts[cluster.part].material;
                        submesh.previousPositions = skinned.firstSkinVertex + 1;
                        submesh.pad1 = 1; // A cluster: no structure or micromaps of its own
                        submeshes.push_back(submesh);

                        Fox::Graphics::Vulkan::RayTracing::ClusterLodPass::GpuCluster gpu{};
                        gpu.selfBounds = cluster.selfBounds;
                        gpu.parentBounds = cluster.parentBounds;
                        gpu.selfError = cluster.selfError;
                        gpu.parentError = cluster.parentError >= 3.0e38f ? 3.0e38f : cluster.parentError;
                        gpu.gatherFirst = localVertexBase + cluster.firstVertex;
                        gpu.gatherCount = cluster.vertexCount;
                        clusterContent.clusters.push_back(gpu);

                        // Its build: 8 bit indices into its gathered vertices (addresses as offsets until the upload)
                        VkClusterAccelerationStructureBuildTriangleClusterInfoNV info{};
                        info.clusterID = static_cast<uint32_t>(clusterContent.templates.size());
                        info.triangleCount = cluster.triangleCount;
                        info.vertexCount = cluster.vertexCount;
                        info.indexType = VK_CLUSTER_ACCELERATION_STRUCTURE_INDEX_FORMAT_8BIT_NV;
                        info.baseGeometryIndexAndGeometryFlags.geometryIndex = j;
                        info.baseGeometryIndexAndGeometryFlags.geometryFlags = cluster.foliage ? 0u : VK_CLUSTER_ACCELERATION_STRUCTURE_GEOMETRY_OPAQUE_BIT_NV;
                        info.indexBufferStride = 1;
                        info.vertexBufferStride = sizeof(float) * 3;
                        info.indexBuffer = localIndexBase + cluster.firstIndex;
                        info.vertexBuffer = (localVertexBase + cluster.firstVertex) * sizeof(float) * 3;
                        clusterContent.templates.push_back(info);

                        clusterContent.maxTrianglesPerCluster = std::max(clusterContent.maxTrianglesPerCluster, cluster.triangleCount);
                        clusterContent.maxVerticesPerCluster = std::max(clusterContent.maxVerticesPerCluster, cluster.vertexCount);
                        clusterContent.totalTriangles += cluster.triangleCount;
                        radius = std::max(radius, glm::length(glm::vec3(cluster.selfBounds) - center) + cluster.selfBounds.w);
                        if (cluster.parentError < 3.0e38f) {
                            radius = std::max(radius, glm::length(glm::vec3(cluster.parentBounds) - center) + cluster.parentBounds.w);
                        }
                    }
                    clusterMesh.bounds = glm::vec4(center, radius);
                    for (auto level : lod->levels) {
                        level.firstCluster += clusterMesh.firstCluster;
                        clusterMesh.levels.push_back(level);
                    }
                    clusterContent.maxClustersPerMesh = std::max(clusterContent.maxClustersPerMesh, clusterMesh.clusterCount);
                    clusterMeshes.push_back(std::move(clusterMesh));
                }
                for (const auto& part : clustered ? std::vector<Fox::Graphics::Geometry::SkinnedMesh::Part>{} : parts) {
                    const Fox::Graphics::Vulkan::Material& material = meshMaterials.at(part.material);
                    if (KindOf(material) != Opaque) {
                        throw std::runtime_error("Skinned meshes support opaque and alpha tested materials only");
                    }

                    Fox::Graphics::Vulkan::Submesh submesh{};
                    submesh.vertexOffset = vertexOffset;
                    submesh.vertexCount = skinned.vertexCount;
                    submesh.indexOffset = indexOffset + part.firstIndex;
                    submesh.indexCount = part.indexCount;
                    submesh.materialIndex = materialOffset + part.material;
                    submesh.previousPositions = skinned.firstSkinVertex + 1;
                    submeshes.push_back(submesh);
                    record.count[Opaque]++;
                }

                vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
                const std::vector<uint32_t>& meshIndices = clustered ? lod->indices : mesh.indices;
                for (uint32_t index : meshIndices) {
                    indices.push_back(vertexOffset + index);
                }
                for (Fox::Graphics::Vulkan::Material material : meshMaterials) {
                    material.lightIndex = -1;
                    materials.push_back(material);
                }

                vertexOffset += skinned.vertexCount;
                indexOffset += static_cast<uint32_t>(meshIndices.size());
                materialOffset += static_cast<uint32_t>(meshMaterials.size());
                meshRecords.push_back(record);

                Placement placement;
                placement.record = meshRecords.size() - 1;
                placement.transform = Decompose(transform);
                placement.name = name;
                placements.push_back(std::move(placement));

                skinned.object = placements.size() - 1;
                skinned.record = meshRecords.size() - 1;
                skinnedMeshes.push_back(std::move(skinned));
                return skinnedMeshes.size() - 1;
            }

            void RayTracingScene::BuildOpacityMicromaps(uint32_t firstSubmesh, OpacityMicromapSet& set) {
                set = OpacityMicromapSet{};
                if (!opacityMicromapsEnabled || alphaMasks.empty()) {
                    return;
                }

                // The texture coordinates the shaders use (KHR_texture_transform, as in TransformUV)
                auto transformedUV = [](const Fox::Graphics::Vulkan::Vertex& vertex, const Fox::Graphics::Vulkan::Material& material) {
                    glm::vec2 uv = glm::vec2(vertex.uv[0], vertex.uv[1]) * glm::vec2(material.uvScale[0], material.uvScale[1]);
                    if (material.uvRotation != 0.0f) {
                        const float s = std::sin(material.uvRotation), c = std::cos(material.uvRotation);
                        uv = glm::vec2(c * uv.x + s * uv.y, -s * uv.x + c * uv.y);
                    }
                    return uv;
                };

                set.micromaps = std::make_unique<Fox::Graphics::Vulkan::RayTracing::OpacityMicromapBuilder>();

                struct Pending { uint32_t submesh; size_t firstIndex; std::map<uint32_t, uint32_t> perLevel; };
                std::vector<Pending> pending;
                std::vector<int32_t> micromapIndices; // Per triangle of every alpha tested submesh
                size_t triangleCount = 0;

                const auto start = std::chrono::steady_clock::now();
                for (uint32_t s = firstSubmesh; s < submeshes.size(); s++) {
                    const auto& submesh = submeshes[s];
                    const auto& material = materials[submesh.materialIndex];
                    const auto mask = alphaMasks.find(material.baseColorTexture);
                    if (!material.IsAlphaTested() || mask == alphaMasks.end() || submesh.pad1 == 1) {
                        continue;
                    }

                    Pending entry{ s, micromapIndices.size(), {} };
                    for (uint32_t t = 0; t + 2 < submesh.indexCount; t += 3) {
                        const uint32_t base = submesh.indexOffset + t;
                        const int32_t index = set.micromaps->AddTriangle(mask->second, material.baseColor[3], material.alphaCutoff,
                            transformedUV(vertices[indices[base + 0]], material),
                            transformedUV(vertices[indices[base + 1]], material),
                            transformedUV(vertices[indices[base + 2]], material));
                        micromapIndices.push_back(index);
                        if (index >= 0) {
                            entry.perLevel[set.micromaps->LevelOf(index)]++;
                        }
                    }
                    triangleCount += submesh.indexCount / 3;
                    pending.push_back(std::move(entry));
                }

                if (set.micromaps->EntryCount() == 0) {
                    set.micromaps = nullptr;
                    return;
                }

                const VkCommandPool pool = commandPool->Get();
                set.micromaps->Build(device, physicalDevice, pool, queue);

                // Which micromap entry each triangle uses, read by the acceleration structure builds (and refits)
                set.indexBuffer = std::make_unique<Fox::Graphics::Vulkan::Buffer>(device, physicalDevice, sizeof(int32_t) * micromapIndices.size(),
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                std::memcpy(set.indexBuffer->Map(), micromapIndices.data(), sizeof(int32_t) * micromapIndices.size());
                set.indexBuffer->Unmap();
                const VkDeviceAddress indexAddress = GetBufferAddress(set.indexBuffer->Get());

                for (const Pending& entry : pending) {
                    auto geometry = std::make_unique<OpacityGeometry>();
                    for (const auto& [level, count] : entry.perLevel) {
                        geometry->usage.push_back({ count, level, VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT });
                    }

                    geometry->info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT;
                    geometry->info.indexType = VK_INDEX_TYPE_UINT32;
                    geometry->info.indexBuffer.deviceAddress = indexAddress + sizeof(int32_t) * entry.firstIndex;
                    geometry->info.indexStride = sizeof(int32_t);
                    geometry->info.baseTriangle = 0;
                    geometry->info.usageCountsCount = static_cast<uint32_t>(geometry->usage.size());
                    geometry->info.pUsageCounts = geometry->usage.data();
                    geometry->info.micromap = set.micromaps->Get();
                    opacityGeometries[entry.submesh] = std::move(geometry);
                }

                const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
                std::cout << "Opacity micromaps: " << triangleCount << " triangles, " << set.micromaps->EntryCount() << " entries ("
                    << set.micromaps->DataSize() / 1024 << " KiB), " << seconds << " s\n";
            }

            RayTracingScene::GeometryStats RayTracingScene::GetGeometryStats() const {
                GeometryStats stats;
                std::vector<uint64_t> blasTriangles(bottomLevelAccelerationStructure.size(), 0);
                for (size_t b = 0; b < bottomLevelAccelerationStructure.size(); b++) {
                    const auto& blas = bottomLevelAccelerationStructure[b];
                    if (blas.handle == VK_NULL_HANDLE) {
                        continue;
                    }
                    for (const auto& range : blas.ranges) {
                        blasTriangles[b] += range.primitiveCount;
                    }
                    stats.blasCount++;
                    stats.storedTriangles += blasTriangles[b];
                    stats.blasBytes += blas.sizeInfo.accelerationStructureSize;
                }
                stats.tlasInstances = instances.size();
                for (size_t i = 0; i < instances.size() && i < instanceBlasIndices.size(); i++) {
                    if (instances[i].mask == 0) {
                        continue; // Hidden
                    }
                    stats.visibleInstances++;
                    const uint32_t b = instanceBlasIndices[i];
                    if (b < blasTriangles.size()) {
                        stats.tracedTriangles += blasTriangles[b];
                    }
                }
                if (topLevelAccelerationStructure.buffer != VK_NULL_HANDLE) {
                    VkMemoryRequirements requirements{};
                    vkGetBufferMemoryRequirements(device, topLevelAccelerationStructure.buffer, &requirements);
                    stats.tlasBytes = requirements.size;
                }

                // Trees traced through clusters: the clusters selected over all their instances, at their average size
                if (clusterPass) {
                    const auto lod = clusterPass->GetStats();
                    uint64_t clusterSubmeshes = 0, clusterIndexCount = 0;
                    for (const auto& submesh : submeshes) {
                        if (submesh.pad1 == 1) {
                            clusterSubmeshes++;
                            clusterIndexCount += submesh.indexCount;
                        }
                    }
                    stats.clusterInstances = lod.instances;
                    stats.clustersSelected = lod.clustersSelected;
                    stats.clusterBytes = lod.memory;
                    if (clusterSubmeshes > 0) {
                        stats.clusterTriangles = static_cast<uint64_t>(static_cast<double>(lod.clustersSelected) * clusterIndexCount / 3.0 / clusterSubmeshes);
                        stats.tracedTriangles += stats.clusterTriangles;
                    }
                }

                stats.objects = placements.size();
                stats.vertices = vertices.size();
                stats.materials = materials.size();
                stats.lights = lightCount;
                stats.geometryBytes = vertices.size() * sizeof(Fox::Graphics::Vulkan::Vertex) * 2 + indices.size() * sizeof(uint32_t);
                return stats;
            }

            void RayTracingScene::SetJointMatrices(size_t skinnedMesh, const std::vector<glm::mat4>& skinMatrices) {
                SkinnedRecord& skinned = skinnedMeshes[skinnedMesh];
                const size_t count = std::min(skinMatrices.size(), skinned.joints.size());
                for (size_t j = 0; j < count; j++) {
                    const glm::mat4& m = skinMatrices[j];
                    for (int row = 0; row < 3; row++) {
                        skinned.joints[j].rows[row] = glm::vec4(m[0][row], m[1][row], m[2][row], m[3][row]);
                    }
                }
            }

            void RayTracingScene::SetSkinGrowth(size_t skinnedMesh, float leafFront, float leafSpan) {
                SkinnedRecord& skinned = skinnedMeshes[skinnedMesh];
                skinned.leafFront = leafFront;
                skinned.leafSpan = std::max(leafSpan, 1e-4f);
            }

            void RayTracingScene::SetSkinUpdate(size_t skinnedMesh, SkinUpdate update) {
                SkinnedRecord& skinned = skinnedMeshes[skinnedMesh];
                if (update != SkinUpdate::Freeze) {
                    skinned.settled = false;
                }
                skinned.update = update;
            }

            void RayTracingScene::RecordSkinning(VkCommandBuffer cmd) {
                // Which deform this frame (wind level of detail: distant trees less often, the farthest not at all)
                auto deforms = [](const SkinnedRecord& skinned) {
                    return skinned.update == SkinUpdate::Deform || (skinned.update == SkinUpdate::Freeze && !skinned.settled);
                };

                // The previous frame may still be tracing against these vertices and structures
                VkMemoryBarrier beforeSkinning{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &beforeSkinning, 0, nullptr, 0, nullptr);

                for (const SkinnedRecord& skinned : skinnedMeshes) {
                    if (!deforms(skinned)) {
                        continue;
                    }
                    const bool refitted = meshRecords[skinned.record].clusterMesh < 0;
                    skinningPass->Dispatch(cmd, { skinned.firstVertex, skinned.vertexCount, skinned.firstSkinVertex, skinned.firstJoint,
                        skinned.leafFront, skinned.leafSpan, refitted ? 1.0f : 0.0f, 0.0f });
                }

                // The refits read the new vertices, and so do the ray tracing shaders
                VkMemoryBarrier afterSkinning{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                    0, 1, &afterSkinning, 0, nullptr, 0, nullptr);

                // Refit: same triangles, new positions. Much cheaper than a rebuild, and fine for bending.
                for (SkinnedRecord& skinned : skinnedMeshes) {
                    if (!deforms(skinned)) {
                        continue;
                    }
                    if (skinned.update == SkinUpdate::Freeze) {
                        skinned.settled = true; // Caught up with this dispatch
                    }
                    for (int blasIndex : meshRecords[skinned.record].blas) {
                        if (blasIndex < 0) {
                            continue;
                        }
                        BottomLevelAccelerationStructure& blas = bottomLevelAccelerationStructure[blasIndex];

                        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
                            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
                            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,
                            .srcAccelerationStructure = blas.handle,
                            .dstAccelerationStructure = blas.handle,
                            .geometryCount = static_cast<uint32_t>(blas.geometries.size()),
                            .pGeometries = blas.geometries.data(),
                            .scratchData = {.deviceAddress = blasScratchAddress }
                        };
                        const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { blas.ranges.data() };
                        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, ranges);

                        // The refits share the scratch memory, and the top level build reads the result
                        VkMemoryBarrier afterRefit{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                            VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                            VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR };
                        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                            0, 1, &afterRefit, 0, nullptr, 0, nullptr);
                    }
                }

                // The top level structure stores bounds of the bottom level ones: rebuild it too
                tlasDirty = true;
            }

            void RayTracingScene::SetObjectTransform(size_t object, const ObjectTransform& transform) {
                Placement& placement = placements[object];
                if (!placement.movable) {
                    return;
                }

                placement.transform = transform;
                placement.transform.rotation = glm::normalize(transform.rotation);
                tlasDirty = true;
                lightsDirty = lightsDirty || !placement.localLights.empty();
            }

            void RayTracingScene::SetObjectVisible(size_t object, bool visible) {
                Placement& placement = placements[object];
                if (placement.visible == visible) {
                    return;
                }
                placement.visible = visible;
                for (size_t i = 0; i < placement.instances.size() && i < placement.masks.size(); i++) {
                    instances[placement.instances[i]].mask = visible ? placement.masks[i] : 0u;
                }
                tlasDirty = true;
                lightsDirty = lightsDirty || !placement.localLights.empty();
            }

            void RayTracingScene::RecordFrameUpdate(VkCommandBuffer cmd, uint32_t frameSlot, const std::function<void(const char*)>& mark) {
                // Instance motion since the previous frame (for motion vectors), and lights of moved objects.
                // Small, so they are written into the command buffer (vkCmdUpdateBuffer, up to 64 KiB).
                std::vector<InstanceMotion> motion(instancePlacements.size());
                // Skinned meshes held this frame (wind level of detail) keep their shape: no motion of their own, whatever
                // the previous positions (from their last deformation) say
                std::vector<bool> held(meshRecords.size(), false);
                for (const SkinnedRecord& skinned : skinnedMeshes) {
                    held[skinned.record] = skinned.update == SkinUpdate::Hold || (skinned.update == SkinUpdate::Freeze && skinned.settled);
                }
                for (Placement& placement : placements) {
                    const glm::mat4 current = placement.TracedMatrix();
                    const glm::mat4 toPrevious = placement.previousMatrix * glm::inverse(current);
                    for (uint32_t instance : placement.instances) {
                        for (int row = 0; row < 3; row++) {
                            motion[instance].rows[row] = glm::vec4(toPrevious[0][row], toPrevious[1][row], toPrevious[2][row], toPrevious[3][row]);
                        }
                    }
                    placement.previousMatrix = current;
                    // Instanced objects' surfaces are not sampled as lights: a hit gets no light index (full weight)
                    const int lightOffset = !placement.items.empty() ? -(1 << 30)
                        : static_cast<int>(placement.firstLight) - static_cast<int>(meshRecords[placement.record].firstLight);
                    for (uint32_t instance : placement.instances) {
                        motion[instance].lights = glm::ivec4(lightOffset, held[placement.record] ? 1 : 0, 0, 0);
                    }
                }

                if (lightsDirty) {
                    for (const Placement& placement : placements) {
                        for (size_t i = 0; i < placement.localLights.size(); i++) {
                            lights[placement.firstLight + i] = TransformLight(placement.localLights[i], placement.transform.Matrix());
                            if (!placement.visible) {
                                lights[placement.firstLight + i].color = glm::vec3(0.0f); // Dark while hidden
                            }
                        }
                    }
                    BuildLightSelection();
                }

                const VkDeviceSize motionSize = sizeof(InstanceMotion) * motion.size();
                const VkDeviceSize lightsSize = sizeof(Fox::Graphics::Vulkan::Light) * lights.size();
                const bool uploadLights = lightsDirty && lightCount > 0;
                lightsDirty = false;

                // The previous frame may still be reading these buffers
                VkMemoryBarrier beforeWrite{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 1, &beforeWrite, 0, nullptr, 0, nullptr);

                // Only the pieces that changed: with clutter there are many instances, most of them still
                const VkDeviceSize uploadedSize = sizeof(InstanceMotion) * uploadedMotion.size();
                for (VkDeviceSize done = 0; done < motionSize; done += 65536) {
                    const VkDeviceSize size = std::min<VkDeviceSize>(65536, motionSize - done);
                    const uint8_t* data = reinterpret_cast<const uint8_t*>(motion.data()) + done;
                    if (done + size <= uploadedSize && std::memcmp(data, reinterpret_cast<const uint8_t*>(uploadedMotion.data()) + done, size) == 0) {
                        continue;
                    }
                    vkCmdUpdateBuffer(cmd, instanceMotionSSBO->GetBuffer().Get(), done, size, data);
                }
                uploadedMotion = std::move(motion);
                if (uploadLights) {
                    for (VkDeviceSize done = 0; done < lightsSize; done += 65536) {
                        vkCmdUpdateBuffer(cmd, lightsSSBO->GetBuffer().Get(), done, std::min<VkDeviceSize>(65536, lightsSize - done),
                            reinterpret_cast<const uint8_t*>(lights.data()) + done);
                    }
                }

                // Joint matrices of the skinned meshes (in pieces: vkCmdUpdateBuffer takes up to 64 KiB)
                if (jointsSSBO) {
                    for (const SkinnedRecord& skinned : skinnedMeshes) {
                        const uint8_t* data = reinterpret_cast<const uint8_t*>(skinned.joints.data());
                        const VkDeviceSize size = sizeof(JointMatrix) * skinned.joints.size();
                        const VkDeviceSize offset = sizeof(JointMatrix) * skinned.firstJoint;
                        for (VkDeviceSize done = 0; done < size; done += 65536) {
                            vkCmdUpdateBuffer(cmd, jointsSSBO->GetBuffer().Get(), offset + done, std::min<VkDeviceSize>(65536, size - done), data + done);
                        }
                    }
                }

                VkMemoryBarrier afterWrite{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &afterWrite, 0, nullptr, 0, nullptr);

                // Edited surfaces (terrain), materials and textures
                // Simulations writing geometry (water), then what the edits and simulations changed
                RecordSimulations(cmd, frameSlot);
                if (mark) mark("simulate");
                RecordEdits(cmd, frameSlot, mark);
                if (mark) mark("edits");

                // Deform the skinned meshes every frame (also when their joints did not change: the previous
                // positions must catch up, or a mesh that stopped would keep its last motion vectors)
                if (skinningPass) {
                    RecordSkinning(cmd);
                    if (mark) mark("skin");
                }

                // Cluster instances get new structures every frame (their level of detail, the wind)
                const bool clusters = clusterPass && !clusterMeshes.empty();
                instancesWrittenThisFrame = false;
                if (!tlasDirty && !clusters) {
                    return;
                }
                tlasDirty = false;

                // Rebuild the top level structure in place. It holds one instance per object and surface kind,
                // so a full rebuild costs microseconds and keeps tracing as fast as the original build.
                for (const Placement& placement : placements) {
                    const glm::mat4 traced = placement.TracedMatrix();
                    for (size_t k = 0; k < placement.instances.size(); k++) {
                        const glm::mat4 transform = placement.instanceItems.empty() ? traced : traced * placement.items[placement.instanceItems[k]];
                        instances[placement.instances[k]].transform = {
                            transform[0][0], transform[1][0], transform[2][0], transform[3][0],
                            transform[0][1], transform[1][1], transform[2][1], transform[3][1],
                            transform[0][2], transform[1][2], transform[2][2], transform[3][2]
                        };
                    }
                }

                // Its own instance buffer per frame in flight: the previous frame's build may still read the other
                if (frameInstanceBuffers.size() <= frameSlot) {
                    frameInstanceBuffers.resize(frameSlot + 1);
                }
                auto& instanceData = frameInstanceBuffers[frameSlot];
                const VkDeviceSize instancesSize = sizeof(VkAccelerationStructureInstanceKHR) * instances.size();
                if (!instanceData) {
                    instanceData = std::make_unique<Fox::Graphics::Vulkan::Buffer>(device, physicalDevice, sizeof(VkAccelerationStructureInstanceKHR) * MAX_INSTANCES,
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, // The cluster pass writes its instances' addresses
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                }
                std::memcpy(instanceData->Map(), instances.data(), instancesSize);
                instanceData->Unmap();
                lastInstanceSlot = frameSlot;
                instancesWrittenThisFrame = true;
                if (clusters) {
                    RecordClusterLod(cmd, frameSlot, instanceData->Get(), mark);
                }

                VkAccelerationStructureGeometryInstancesDataKHR instancesData{
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                    .arrayOfPointers = VK_FALSE,
                    .data = { GetBufferAddress(instanceData->Get()) }
                };

                VkAccelerationStructureGeometryKHR geometry{
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                    .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
                    .geometry = {.instances = instancesData }
                };

                VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                    .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
                    .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
                    .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
                    .dstAccelerationStructure = topLevelAccelerationStructure.handle,
                    .geometryCount = 1,
                    .pGeometries = &geometry,
                    .scratchData = {.deviceAddress = tlasScratchAddress }
                };

                VkAccelerationStructureBuildRangeInfoKHR range{ .primitiveCount = static_cast<uint32_t>(instances.size()) };
                const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { &range };

                // The previous frame's rays must be done with the structure before it is overwritten
                VkMemoryBarrier beforeBuild{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                    VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                    VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &beforeBuild, 0, nullptr, 0, nullptr);

                vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, ranges);
                if (mark) mark("tlas");

                VkMemoryBarrier afterBuild{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                    VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &afterBuild, 0, nullptr, 0, nullptr);
            }

            void RayTracingScene::AddGeneratedSurface(const std::vector<Fox::Graphics::Vulkan::Vertex>& surfaceVertices,
                const std::vector<uint32_t>& surfaceIndices, const Fox::Graphics::Vulkan::Material& material,
                const glm::mat4& transform, const std::string& name, bool movable) {
                vertices.insert(vertices.end(), surfaceVertices.begin(), surfaceVertices.end());
                for (uint32_t index : surfaceIndices) {
                    indices.push_back(vertexOffset + index);
                }

                Fox::Graphics::Vulkan::Submesh submesh{};
                submesh.vertexOffset = vertexOffset;
                submesh.indexOffset = indexOffset;
                submesh.vertexCount = static_cast<uint32_t>(surfaceVertices.size());
                submesh.indexCount = static_cast<uint32_t>(surfaceIndices.size());
                submesh.materialIndex = materialOffset;

                MeshRecord record;
                record.firstSubmesh = static_cast<uint32_t>(submeshes.size());
                record.count[KindOf(material)] = 1;
                meshRecords.push_back(record);

                Placement placement;
                placement.record = meshRecords.size() - 1;
                placement.transform = Decompose(transform);
                placement.name = name;
                placement.movable = movable;
                placements.push_back(std::move(placement));

                submeshes.push_back(submesh);

                Fox::Graphics::Vulkan::Material surfaceMaterial = material;
                surfaceMaterial.lightIndex = -1;
                materials.push_back(surfaceMaterial);

                // Glowing: its triangles are lights
                MeshRecord& added = meshRecords.back();
                Placement& placed = placements.back();
                added.firstLight = placed.firstLight = static_cast<uint32_t>(lights.size());
                added.triangleLights = MakeTriangleLights(added.firstSubmesh, 1, placed.firstLight);
                placed.localLights = added.triangleLights;
                for (const Fox::Graphics::Vulkan::Light& light : placed.localLights) {
                    lights.push_back(TransformLight(light, transform));
                }

                vertexOffset += submesh.vertexCount;
                indexOffset += submesh.indexCount;
                materialOffset += 1;
            }

            size_t RayTracingScene::AddGeneratedObject(const std::vector<Fox::Graphics::Vulkan::Vertex>& surfaceVertices,
                const std::vector<uint32_t>& surfaceIndices, const Fox::Graphics::Vulkan::Material& material, const glm::mat4& transform, const std::string& name) {
                AddGeneratedSurface(surfaceVertices, surfaceIndices, material, transform, name, true);
                return placements.size() - 1;
            }

            void RayTracingScene::AddGroundPlane(float height, float halfSize, const Fox::Graphics::Vulkan::Material& material) {
                const float corners[4][2] = { { -1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, -1.0f } };

                std::vector<Fox::Graphics::Vulkan::Vertex> planeVertices;
                for (const auto& corner : corners) {
                    Fox::Graphics::Vulkan::Vertex vertex{};
                    vertex.position[0] = corner[0] * halfSize;
                    vertex.position[1] = height;
                    vertex.position[2] = corner[1] * halfSize;
                    vertex.normal[1] = 1.0f;
                    vertex.uv[0] = corner[0] * 0.5f + 0.5f;
                    vertex.uv[1] = corner[1] * 0.5f + 0.5f;
                    planeVertices.push_back(vertex);
                }

                AddGeneratedSurface(planeVertices, { 0u, 1u, 2u, 0u, 2u, 3u }, material, glm::mat4(1.0f), "Ground", false);
            }

            void RayTracingScene::AddSphere(const glm::vec3& center, float radius, const Fox::Graphics::Vulkan::Material& material,
                const std::string& name, uint32_t segments) {
                // UV sphere: rings of latitude from the north pole (+Y) to the south pole
                const uint32_t columns = std::max(segments, 3u);
                const uint32_t rows = std::max(segments / 2, 2u);

                std::vector<Fox::Graphics::Vulkan::Vertex> sphereVertices;
                sphereVertices.reserve((rows + 1) * (columns + 1));

                for (uint32_t row = 0; row <= rows; row++) {
                    const float v = static_cast<float>(row) / static_cast<float>(rows);
                    const float theta = v * glm::pi<float>();

                    for (uint32_t column = 0; column <= columns; column++) {
                        const float u = static_cast<float>(column) / static_cast<float>(columns);
                        const float phi = u * glm::two_pi<float>();

                        // Outward unit normal; the tangent follows increasing u (towards increasing phi)
                        const glm::vec3 normal(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
                        // Around the origin: the object's transform places it, so it moves and scales about its center
                        const glm::vec3 position = normal * radius;

                        Fox::Graphics::Vulkan::Vertex vertex{};
                        vertex.position[0] = position.x;
                        vertex.position[1] = position.y;
                        vertex.position[2] = position.z;
                        vertex.normal[0] = normal.x;
                        vertex.normal[1] = normal.y;
                        vertex.normal[2] = normal.z;
                        vertex.uv[0] = u;
                        vertex.uv[1] = v;
                        vertex.tangent[0] = -std::sin(phi);
                        vertex.tangent[2] = std::cos(phi);
                        vertex.tangent[3] = 1.0f;
                        sphereVertices.push_back(vertex);
                    }
                }

                // Counter-clockwise seen from outside, so the triangle normals point outwards like the vertex
                // normals: refraction decides from them whether a ray enters or leaves the glass
                std::vector<uint32_t> sphereIndices;
                sphereIndices.reserve(rows * columns * 6);
                for (uint32_t row = 0; row < rows; row++) {
                    for (uint32_t column = 0; column < columns; column++) {
                        const uint32_t a = row * (columns + 1) + column; // Upper ring
                        const uint32_t b = a + columns + 1;              // Lower ring
                        if (row > 0) {
                            sphereIndices.insert(sphereIndices.end(), { a, a + 1, b });
                        }
                        if (row < rows - 1) {
                            sphereIndices.insert(sphereIndices.end(), { a + 1, b + 1, b });
                        }
                    }
                }

                AddGeneratedSurface(sphereVertices, sphereIndices, material, glm::translate(glm::mat4(1.0f), center), name, true);
            }

            void RayTracingScene::AddSun(const glm::vec3& directionToSun, const glm::vec3& irradiance, float angularRadius) {
                Fox::Graphics::Vulkan::Light sun{};
                sun.type = static_cast<int>(Fox::Core::Loaders::GLTF::LightType::Directional);
                sun.normal = glm::normalize(directionToSun);
                sun.color = irradiance;
                // Directional lights have no area; the field holds the angular radius of the disc in radians
                sun.area = glm::radians(angularRadius);
                lights.push_back(sun);
            }

            size_t RayTracingScene::AddLight(const Fox::Graphics::Vulkan::Light& light) {
                lights.push_back(light);
                return lights.size() - 1;
            }

            void RayTracingScene::SetLight(size_t index, const Fox::Graphics::Vulkan::Light& light) {
                if (index < lightCount && index < lights.size()) {
                    lights[index] = light;
                    lightsDirty = true;
                }
            }

            void RayTracingScene::UploadGeometry() {
                // The scene is always lit by the environment map; analytic lights are optional.
                lightCount = static_cast<uint32_t>(lights.size());

                // A storage buffer cannot be empty, so keep one unused entry around.
                if (lights.empty()) {
                    lights.push_back(Fox::Graphics::Vulkan::Light{});
                }
                BuildLightSelection();

                // What is here now is the static scene; dynamic meshes (RebuildDynamicMeshes) go after it
                staticCounts.vertices = static_cast<uint32_t>(vertices.size());
                staticCounts.indices = static_cast<uint32_t>(indices.size());
                staticCounts.submeshes = static_cast<uint32_t>(submeshes.size());
                staticCounts.materials = static_cast<uint32_t>(materials.size());
                staticCounts.records = meshRecords.size();
                staticCounts.placements = placements.size();
                staticCounts.skinnedMeshes = skinnedMeshes.size();
                staticCounts.bindPose = bindPose.size();
                staticCounts.joints = jointCount;

                if (submeshes.size() > MAX_SUBMESHES || materials.size() > MAX_MATERIALS) {
                    throw std::runtime_error("The static scene has more submeshes or materials than the scene buffers hold");
                }

                // Buffers with room for the dynamic meshes. The arrays grow for the upload (zeros) and shrink back.
                vertices.resize(staticCounts.vertices + DYNAMIC_VERTEX_CAPACITY);
                indices.resize(staticCounts.indices + DYNAMIC_INDEX_CAPACITY);

                // Acceleration structure build inputs
                auto vertexBuffer = std::make_unique<Fox::Graphics::Vulkan::VertexBuffer>();
                vertexBuffer->Create(device, physicalDevice, commandPool->Get(), queue, vertices, "Raytracing Vertex Buffer");
                vertexBuffers.push_back(std::move(vertexBuffer));

                auto indexBuffer = std::make_unique<Fox::Graphics::Vulkan::IndexBuffer>();
                indexBuffer->Create(device, physicalDevice, commandPool->Get(), queue, indices, "Raytracing Index Buffer");
                indexBuffers.push_back(std::move(indexBuffer));

                // Shader visible copies of the scene data, in video memory: every ray hit reads them
                using Fox::Graphics::Vulkan::ShaderStorageBuffer;
                const VkCommandPool pool = commandPool->Get();
                vertexSSBO = ShaderStorageBuffer<Fox::Graphics::Vulkan::Vertex>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Scene Vertex SSBO", vertices);
                indexSSBO = ShaderStorageBuffer<uint32_t>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Scene Index SSBO", indices);

                vertices.resize(staticCounts.vertices);
                vertices.shrink_to_fit();
                indices.resize(staticCounts.indices);
                indices.shrink_to_fit();

                std::vector<Fox::Graphics::Vulkan::Submesh> submeshCapacity = submeshes;
                submeshCapacity.resize(MAX_SUBMESHES);
                submeshSSBO = ShaderStorageBuffer<Fox::Graphics::Vulkan::Submesh>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Scene Submesh SSBO", submeshCapacity);

                std::vector<Fox::Graphics::Vulkan::Material> materialCapacity = materials;
                materialCapacity.resize(MAX_MATERIALS);
                materialsSSBO = ShaderStorageBuffer<Fox::Graphics::Vulkan::Material>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Scene Materials", materialCapacity);

                lightsSSBO = ShaderStorageBuffer<Fox::Graphics::Vulkan::Light>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Scene Lights", lights);

                VkDeviceAddress vertexAddress = Fox::Graphics::Vulkan::GetBufferDeviceAddress(device, vertexBuffers.back()->Get());
                VkDeviceAddress indexAddress = Fox::Graphics::Vulkan::GetBufferDeviceAddress(device, indexBuffers.back()->Get());

                // Opacity micromaps for alpha tested surfaces: before the BLAS, which reference them
                BuildOpacityMicromaps(0, staticMicromaps);

                // One BLAS per mesh and surface kind, shared by all placements of the mesh. Each submesh is one
                // geometry of its BLAS: the closest hit shader finds the submesh as InstanceID() + GeometryIndex(),
                // so an instance's custom index is the index of its first submesh.
                for (MeshRecord& record : meshRecords) {
                    for (int kind = 0; kind < SurfaceKindCount; kind++) {
                        if (record.count[kind] > 0) {
                            record.blas[kind] = static_cast<int>(AddBLAS(vertexAddress, indexAddress, record.FirstOf(kind), record.count[kind], record.dynamic));
                        }
                    }
                }
                staticCounts.blas = bottomLevelAccelerationStructure.size();

                for (size_t p = 0; p < placements.size(); p++) {
                    AddPlacementInstances(p);
                }
                staticCounts.instances = instances.size();

                // No motion yet: identity for every instance (room for all the instances there can be)
                std::vector<InstanceMotion> identity(MAX_INSTANCES);
                for (InstanceMotion& motion : identity) {
                    motion.rows[0] = glm::vec4(1, 0, 0, 0);
                    motion.rows[1] = glm::vec4(0, 1, 0, 0);
                    motion.rows[2] = glm::vec4(0, 0, 1, 0);
                }
                instanceMotionSSBO = ShaderStorageBuffer<InstanceMotion>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Instance Motion", identity);
                uploadedMotion.clear();

                // Skinning: the previous positions start out at the bind pose. Room for every skinned vertex there
                // can be; the shaders bind it even without skinned meshes.
                std::vector<glm::vec4> previous(std::max<size_t>(bindPose.size(), 1) + DYNAMIC_VERTEX_CAPACITY, glm::vec4(0.0f));
                for (size_t i = 0; i < bindPose.size(); i++) {
                    previous[i] = glm::vec4(bindPose[i].position[0], bindPose[i].position[1], bindPose[i].position[2], 1.0f);
                }
                previousPositionsSSBO = ShaderStorageBuffer<glm::vec4>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Previous Skinned Positions", previous);

                CreateSkinningResources();
            }

            void RayTracingScene::RecordClusterLod(VkCommandBuffer cmd, uint32_t frameSlot, VkBuffer tlasInstances, const std::function<void(const char*)>& mark) {
                using Instance = Fox::Graphics::Vulkan::RayTracing::ClusterLodPass::Instance;

                // Per instance: the levels whose clusters its distance could pick. A level can only be in the cut if
                // some cluster is fine enough seen from as far as the mesh reaches, while its parent is still too
                // coarse seen from as near (cluster bounds lie inside the mesh's bounds). The GPU then tests only
                // these clusters, each against its own bounds.
                auto candidates = [&](const ClusterMesh& mesh, const glm::mat4& toWorld, float scale, float threshold, uint32_t& first, uint32_t& count) {
                    const glm::vec3 center = glm::vec3(toWorld * glm::vec4(glm::vec3(mesh.bounds), 1.0f));
                    const float distance = glm::length(center - clusterView.cameraPosition);
                    const float radius = mesh.bounds.w * scale;
                    const float nearest = std::max(distance - radius, 0.01f), farthest = std::max(distance + radius, 0.01f);
                    const float toPixels = scale * clusterView.pixelScale;
                    int low = -1, high = -1;
                    // The rays' threshold, and the hybrid raster's coarser one (its pixels are larger): both cuts' levels
                    const float thresholds[2] = { threshold, rasterLodScale > 1.0f ? threshold * rasterLodScale : threshold };
                    for (int l = 0; l < static_cast<int>(mesh.levels.size()); l++) {
                        const auto& level = mesh.levels[l];
                        for (float t : thresholds) {
                            const bool fineEnough = level.minSelfError * toPixels / farthest <= t;
                            const bool parentTooCoarse = level.maxParentError >= 3.0e38f || level.maxParentError * toPixels / nearest > t;
                            if (fineEnough && parentTooCoarse) {
                                low = low < 0 ? l : std::min(low, l);
                                high = std::max(high, l);
                            }
                        }
                    }
                    if (low < 0) {
                        low = high = static_cast<int>(mesh.levels.size()) - 1; // Coarser than everything: the top
                    }
                    first = mesh.levels[low].firstCluster;
                    count = mesh.levels[high].firstCluster + mesh.levels[high].clusterCount - first;
                };

                struct Entry {
                    size_t placement;
                    float scale;
                };
                std::vector<Entry> entries;
                for (size_t p = staticCounts.placements; p < placements.size(); p++) {
                    const Placement& placement = placements[p];
                    if (meshRecords[placement.record].clusterMesh >= 0 && placement.visible && !placement.instances.empty()) {
                        const glm::vec3 s = glm::abs(placement.transform.scale);
                        entries.push_back({ p, std::max(s.x, std::max(s.y, s.z)) });
                    }
                }
                if (entries.empty()) {
                    return;
                }

                // Within the room for cluster lists: if every instance's candidates do not fit, coarser everywhere
                std::vector<Instance> list(entries.size());
                float threshold = clusterThreshold * clusterOverload;
                uint32_t total = 0;
                for (int attempt = 0; attempt < 12; attempt++, threshold *= 2.0f) {
                    total = 0;
                    for (size_t e = 0; e < entries.size(); e++) {
                        const Placement& placement = placements[entries[e].placement];
                        const ClusterMesh& mesh = clusterMeshes[meshRecords[placement.record].clusterMesh];
                        const glm::mat4 toWorld = placement.TracedMatrix();
                        Instance& instance = list[e];
                        for (int row = 0; row < 3; row++) {
                            instance.rows[row] = glm::vec4(toWorld[0][row], toWorld[1][row], toWorld[2][row], toWorld[3][row]);
                        }
                        instance.scale = entries[e].scale;
                        instance.threshold = threshold;
                        candidates(mesh, toWorld, entries[e].scale, threshold, instance.firstCandidate, instance.candidateCount);
                        instance.workBase = total;
                        instance.refBase = total;
                        instance.tlasInstance = placement.instances[0];
                        instance.pad = 0;
                        total += instance.candidateCount;
                    }
                    if (total <= clusterPass->GetRefCapacity()) {
                        break;
                    }
                }
                clusterPass->Record(cmd, frameSlot, list, total, tlasInstances, clusterView, mark);
            }

            void RayTracingScene::AddPlacementInstances(size_t p) {
                const uint32_t masks[SurfaceKindCount] = { INSTANCE_MASK_OPAQUE, INSTANCE_MASK_GLASS, INSTANCE_MASK_SOLID_GLASS };
                Placement& placement = placements[p];
                const MeshRecord& record = meshRecords[placement.record];
                placement.previousMatrix = placement.transform.Matrix();
                placement.instances.clear();
                placement.masks.clear();
                if (record.clusterMesh >= 0) {
                    // One instance; its bottom level structure is the cluster pass's, remade every frame
                    placement.instances.push_back(static_cast<uint32_t>(instances.size()));
                    placement.masks.push_back(INSTANCE_MASK_OPAQUE);
                    instancePlacements.push_back(static_cast<uint32_t>(p));
                    AddInstance(NO_BLAS, placement.previousMatrix, record.firstSubmesh, placement.visible ? INSTANCE_MASK_OPAQUE : 0u);
                    return;
                }
                placement.instanceItems.clear();
                for (int kind = 0; kind < SurfaceKindCount; kind++) {
                    if (record.blas[kind] < 0) {
                        continue;
                    }
                    // An instanced object: an instance per copy
                    const size_t copies = placement.items.empty() ? 1 : placement.items.size();
                    for (size_t item = 0; item < copies; item++) {
                        const uint32_t mask = placement.maskOverride ? placement.maskOverride : masks[kind];
                        placement.instances.push_back(static_cast<uint32_t>(instances.size()));
                        placement.masks.push_back(mask);
                        instancePlacements.push_back(static_cast<uint32_t>(p));
                        const glm::mat4 matrix = placement.items.empty() ? placement.previousMatrix : placement.previousMatrix * placement.items[item];
                        AddInstance(static_cast<uint32_t>(record.blas[kind]), matrix, record.FirstOf(kind), placement.visible ? mask : 0u);
                        if (!placement.items.empty()) {
                            placement.instanceItems.push_back(static_cast<uint32_t>(item));
                        }
                    }
                }
            }

            void RayTracingScene::CreateSkinningResources() {
                skinningPass = nullptr;
                bindPoseSSBO = nullptr;
                jointsSSBO = nullptr;
                if (skinnedMeshes.empty()) {
                    return;
                }

                std::vector<JointMatrix> joints;
                for (const SkinnedRecord& skinned : skinnedMeshes) {
                    joints.insert(joints.end(), skinned.joints.begin(), skinned.joints.end());
                }

                using Fox::Graphics::Vulkan::ShaderStorageBuffer;
                const VkCommandPool pool = commandPool->Get();
                bindPoseSSBO = ShaderStorageBuffer<SkinVertex>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Skin Bind Pose", bindPose);
                jointsSSBO = ShaderStorageBuffer<JointMatrix>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Joint Matrices", joints);
                skinningPass = std::make_unique<Fox::Graphics::Vulkan::RayTracing::SkinningPass>(device,
                    bindPoseSSBO->GetBufferUnique(), jointsSSBO->GetBufferUnique(), vertexSSBO->GetBufferUnique(),
                    vertexBuffers.back(), previousPositionsSSBO->GetBufferUnique());
            }

            void RayTracingScene::DestroyBLAS(BottomLevelAccelerationStructure& blas) {
                if (blas.handle != VK_NULL_HANDLE) {
                    vkDestroyAccelerationStructureKHR(device, blas.handle, nullptr);
                    blas.handle = VK_NULL_HANDLE;
                }
                if (blas.buffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, blas.buffer, nullptr);
                    blas.buffer = VK_NULL_HANDLE;
                }
                if (blas.memory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, blas.memory, nullptr);
                    blas.memory = VK_NULL_HANDLE;
                }
            }

            void RayTracingScene::BuildDynamicBLAS(size_t first) {
                if (first >= bottomLevelAccelerationStructure.size()) {
                    return;
                }

                // Sizes, and scratch memory for building them and for refitting them every frame
                VkDeviceSize scratchSize = 0;
                for (size_t i = first; i < bottomLevelAccelerationStructure.size(); i++) {
                    BottomLevelAccelerationStructure& blas = bottomLevelAccelerationStructure[i];
                    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
                        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                        .flags = BlasBuildFlags(blas),
                        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
                        .geometryCount = uint32_t(blas.geometries.size()),
                        .pGeometries = blas.geometries.data()
                    };
                    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
                    vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, blas.primitiveCounts.data(), &sizeInfo);
                    blas.sizeInfo = sizeInfo;
                    scratchSize = std::max({ scratchSize, sizeInfo.buildScratchSize, blas.dynamic ? sizeInfo.updateScratchSize : VkDeviceSize(0) });
                    CreateAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizeInfo.accelerationStructureSize, blas.handle, blas.buffer, blas.memory);
                }

                if (scratchSize > dynamicScratchSize) {
                    if (dynamicScratchBuffer != VK_NULL_HANDLE) {
                        vkDestroyBuffer(device, dynamicScratchBuffer, nullptr);
                        vkFreeMemory(device, dynamicScratchMemory, nullptr);
                    }
                    CreateScratchBuffer(device, physicalDevice, scratchSize, dynamicScratchBuffer, dynamicScratchMemory, "Dynamic BLAS Scratch");
                    dynamicScratchSize = scratchSize;
                }
                const VkDeviceAddress scratchAddress = GetBufferAddress(dynamicScratchBuffer);
                blasScratchAddress = scratchAddress; // The refits of skinned meshes use it too

                Fox::Graphics::Vulkan::CommandList commandList(device, commandPool->Get());
                commandList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
                for (size_t i = first; i < bottomLevelAccelerationStructure.size(); i++) {
                    BottomLevelAccelerationStructure& blas = bottomLevelAccelerationStructure[i];
                    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
                        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                        .flags = BlasBuildFlags(blas),
                        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
                        .dstAccelerationStructure = blas.handle,
                        .geometryCount = uint32_t(blas.geometries.size()),
                        .pGeometries = blas.geometries.data(),
                        .scratchData = {.deviceAddress = scratchAddress }
                    };
                    const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { blas.ranges.data() };
                    vkCmdBuildAccelerationStructuresKHR(commandList.Get(), 1, &buildInfo, ranges);

                    // The next build reuses the scratch memory
                    VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                        VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR };
                    vkCmdPipelineBarrier(commandList.Get(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        0, 1, &barrier, 0, nullptr, 0, nullptr);
                }
                commandList.End().SubmitAndWait(queue);

                for (size_t i = first; i < bottomLevelAccelerationStructure.size(); i++) {
                    BottomLevelAccelerationStructure& blas = bottomLevelAccelerationStructure[i];
                    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
                        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
                        .accelerationStructure = blas.handle
                    };
                    blas.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &addressInfo);
                }
            }

            std::vector<RayTracingScene::DynamicMeshHandle> RayTracingScene::RebuildDynamicMeshes(const std::vector<DynamicMesh>& meshes) {
                // --- Does it fit? Checked before anything changes ---
                size_t vertexCount = 0, indexCount = 0, partCount = 0, materialCount = 0, placementCount = 0;
                for (const DynamicMesh& entry : meshes) {
                    const bool clustered = entry.lod && clusterLodEnabled && !entry.lod->clusters.empty();
                    vertexCount += entry.mesh->vertices.size();
                    indexCount += clustered ? entry.lod->indices.size() : entry.mesh->indices.size();
                    partCount += clustered ? entry.lod->clusters.size() : std::max<size_t>(entry.mesh->parts.size(), 1);
                    materialCount += entry.materials.size();
                    placementCount += 1 + entry.moreTransforms.size();
                }
                if (vertexCount > DYNAMIC_VERTEX_CAPACITY || indexCount > DYNAMIC_INDEX_CAPACITY) {
                    throw std::runtime_error("Too much geometry for the room reserved for dynamic meshes (" + std::to_string(vertexCount) + " vertices, "
                        + std::to_string(indexCount / 3) + " triangles)");
                }
                if (staticCounts.submeshes + partCount > MAX_SUBMESHES || staticCounts.materials + materialCount > MAX_MATERIALS
                    || staticCounts.instances + placementCount > MAX_INSTANCES) {
                    throw std::runtime_error("Too many dynamic meshes, placements, parts or materials (at most "
                        + std::to_string(MAX_INSTANCES - staticCounts.instances) + " placements)");
                }

                // --- Back to the static scene ---
                for (size_t i = staticCounts.blas; i < bottomLevelAccelerationStructure.size(); i++) {
                    DestroyBLAS(bottomLevelAccelerationStructure[i]);
                }
                bottomLevelAccelerationStructure.resize(staticCounts.blas);
                blasRanges.resize(staticCounts.blas);
                rasterVersion++;

                for (auto it = opacityGeometries.begin(); it != opacityGeometries.end();) {
                    it = it->first >= staticCounts.submeshes ? opacityGeometries.erase(it) : std::next(it);
                }
                dynamicMicromaps = OpacityMicromapSet{};

                vertices.resize(staticCounts.vertices);
                indices.resize(staticCounts.indices);
                submeshes.resize(staticCounts.submeshes);
                materials.resize(staticCounts.materials);
                meshRecords.resize(staticCounts.records);
                placements.resize(staticCounts.placements);
                skinnedMeshes.resize(staticCounts.skinnedMeshes);
                bindPose.resize(staticCounts.bindPose);
                jointCount = staticCounts.joints;
                instances.resize(staticCounts.instances);
                instanceBlasIndices.resize(staticCounts.instances);
                instancePlacements.resize(staticCounts.instances);
                vertexOffset = staticCounts.vertices;
                indexOffset = staticCounts.indices;
                materialOffset = staticCounts.materials;
                clusterMeshes.clear();
                clusterContent = {};

                // --- The new meshes ---
                std::vector<DynamicMeshHandle> handles;
                for (const DynamicMesh& entry : meshes) {
                    DynamicMeshHandle handle;
                    handle.skinnedMesh = AddSkinnedMesh(*entry.mesh, entry.materials, entry.transform.Matrix(), entry.name, entry.lod);
                    handle.object = GetObjectOfSkinnedMesh(handle.skinnedMesh);
                    placements[handle.object].transform = entry.transform; // Exactly, not decomposed from the matrix

                    // Instances: more placements of the same record (geometry, skinning and structures shared)
                    for (size_t i = 0; i < entry.moreTransforms.size(); i++) {
                        Placement placement;
                        placement.record = placements[handle.object].record;
                        placement.transform = entry.moreTransforms[i];
                        placement.name = i < entry.moreNames.size() ? entry.moreNames[i] : entry.name;
                        placements.push_back(std::move(placement));
                        handle.moreObjects.push_back(placements.size() - 1);
                    }
                    handles.push_back(handle);
                }

                // --- Upload what changed, in one go ---
                {
                    struct Upload { VkBuffer target; VkDeviceSize offset; const void* data; VkDeviceSize size; };
                    std::vector<Upload> uploads;
                    const VkDeviceSize vertexBytes = sizeof(Fox::Graphics::Vulkan::Vertex) * (vertices.size() - staticCounts.vertices);
                    const VkDeviceSize vertexOffsetBytes = sizeof(Fox::Graphics::Vulkan::Vertex) * staticCounts.vertices;
                    const VkDeviceSize indexBytes = sizeof(uint32_t) * (indices.size() - staticCounts.indices);
                    const VkDeviceSize indexOffsetBytes = sizeof(uint32_t) * staticCounts.indices;

                    std::vector<glm::vec4> previous(bindPose.size() - staticCounts.bindPose);
                    for (size_t i = 0; i < previous.size(); i++) {
                        const SkinVertex& vertex = bindPose[staticCounts.bindPose + i];
                        previous[i] = glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f);
                    }

                    if (vertexBytes > 0) {
                        uploads.push_back({ vertexSSBO->GetBuffer().Get(), vertexOffsetBytes, vertices.data() + staticCounts.vertices, vertexBytes });
                        uploads.push_back({ vertexBuffers.back()->Get(), vertexOffsetBytes, vertices.data() + staticCounts.vertices, vertexBytes });
                    }
                    if (indexBytes > 0) {
                        uploads.push_back({ indexSSBO->GetBuffer().Get(), indexOffsetBytes, indices.data() + staticCounts.indices, indexBytes });
                        uploads.push_back({ indexBuffers.back()->Get(), indexOffsetBytes, indices.data() + staticCounts.indices, indexBytes });
                    }
                    uploads.push_back({ submeshSSBO->GetBuffer().Get(), 0, submeshes.data(), sizeof(Fox::Graphics::Vulkan::Submesh) * submeshes.size() });
                    uploads.push_back({ materialsSSBO->GetBuffer().Get(), 0, materials.data(), sizeof(Fox::Graphics::Vulkan::Material) * materials.size() });
                    if (!previous.empty()) {
                        uploads.push_back({ previousPositionsSSBO->GetBuffer().Get(), sizeof(glm::vec4) * staticCounts.bindPose, previous.data(), sizeof(glm::vec4) * previous.size() });
                    }

                    VkDeviceSize total = 0;
                    for (const Upload& upload : uploads) {
                        total += upload.size;
                    }
                    Fox::Graphics::Vulkan::Buffer staging(device, physicalDevice, std::max<VkDeviceSize>(total, 4), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                    uint8_t* mapped = static_cast<uint8_t*>(staging.Map());

                    Fox::Graphics::Vulkan::CommandList commandList(device, commandPool->Get());
                    commandList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
                    VkDeviceSize at = 0;
                    for (const Upload& upload : uploads) {
                        std::memcpy(mapped + at, upload.data, upload.size);
                        commandList.CopyBuffer(staging.Get(), upload.target, { VkBufferCopy{ at, upload.offset, upload.size } });
                        at += upload.size;
                    }
                    staging.Unmap();
                    commandList.End().SubmitAndWait(queue);
                }

                CreateSkinningResources();

                // --- Opacity micromaps, then the bottom level structures that use them ---
                BuildOpacityMicromaps(staticCounts.submeshes, dynamicMicromaps);

                const VkDeviceAddress vertexAddress = Fox::Graphics::Vulkan::GetBufferDeviceAddress(device, vertexBuffers.back()->Get());
                const VkDeviceAddress indexAddress = Fox::Graphics::Vulkan::GetBufferDeviceAddress(device, indexBuffers.back()->Get());
                for (size_t r = staticCounts.records; r < meshRecords.size(); r++) {
                    MeshRecord& record = meshRecords[r];
                    if (record.clusterMesh >= 0) {
                        continue; // The cluster pass makes its structures, per instance and frame
                    }
                    for (int kind = 0; kind < SurfaceKindCount; kind++) {
                        if (record.count[kind] > 0) {
                            record.blas[kind] = static_cast<int>(AddBLAS(vertexAddress, indexAddress, record.FirstOf(kind), record.count[kind], record.dynamic));
                        }
                    }
                }
                BuildDynamicBLAS(staticCounts.blas);

                // --- Instances; the top level structure is rebuilt with the next frame ---
                for (size_t p = staticCounts.placements; p < placements.size(); p++) {
                    AddPlacementInstances(p);
                }
                for (size_t i = staticCounts.instances; i < instances.size(); i++) {
                    instances[i].accelerationStructureReference = instanceBlasIndices[i] == NO_BLAS ? 0 : bottomLevelAccelerationStructure[instanceBlasIndices[i]].deviceAddress;
                }
                tlasDirty = true;

                // --- The clusters, to the cluster pass ---
                if (clusterLodEnabled) {
                    clusterContent.instanceCount = 0;
                    for (size_t p = staticCounts.placements; p < placements.size(); p++) {
                        clusterContent.instanceCount += meshRecords[placements[p].record].clusterMesh >= 0 ? 1 : 0;
                    }
                    if (!clusterPass && !clusterMeshes.empty()) {
                        Fox::Graphics::Vulkan::RayTracing::ClusterLodPass::RegisterExtensionFunctions(device);
                        clusterPass = std::make_unique<Fox::Graphics::Vulkan::RayTracing::ClusterLodPass>(device, physicalDevice, commandPool->Get(), queue);
                    }
                    if (clusterPass) {
                        clusterPass->Upload(clusterContent, vertexSSBO->GetBuffer().Get());
                    }
                }

                return handles;
            }

            void RayTracingScene::TransformVertices(uint32_t first, uint32_t count, const glm::mat4& toWorld) {
                if (toWorld == glm::mat4(1.0f)) {
                    return;
                }

                const glm::mat3 linear(toWorld);
                // Normals transform with the inverse transpose, which keeps them perpendicular under non-uniform scale
                const glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
                // A mirroring transform flips which way the bitangent points relative to the normal
                const float handedness = glm::determinant(linear) < 0.0f ? -1.0f : 1.0f;

                for (uint32_t i = first; i < first + count && i < vertices.size(); i++) {
                    Fox::Graphics::Vulkan::Vertex& vertex = vertices[i];

                    const glm::vec3 position = glm::vec3(toWorld * glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f));

                    glm::vec3 normal = normalMatrix * glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
                    if (glm::dot(normal, normal) > 0.0f) {
                        normal = glm::normalize(normal);
                    }

                    glm::vec3 tangent = linear * glm::vec3(vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]);
                    if (glm::dot(tangent, tangent) > 0.0f) {
                        tangent = glm::normalize(tangent);
                    }

                    for (int c = 0; c < 3; c++) {
                        vertex.position[c] = position[c];
                        vertex.normal[c] = normal[c];
                        vertex.tangent[c] = tangent[c];
                    }
                    vertex.tangent[3] *= handedness;
                }
            }

            void RayTracingScene::Build() {
                UploadGeometry();

                Fox::Graphics::Vulkan::CommandList cmdList(device, commandPool->Get());
                cmdList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
                    .BuildAccelerationStructures(*this)
                    .End()
                    .SubmitAndWait(queue);
            }

            // --- Editable surfaces (terrain) ---

            size_t RayTracingScene::AddEditableSurface(const std::vector<Fox::Graphics::Vulkan::Vertex>& surfaceVertices,
                const std::vector<uint32_t>& surfaceIndices, const Fox::Graphics::Vulkan::Material& material,
                const std::vector<Fox::Graphics::Vulkan::Material>& extraMaterials, const glm::mat4& transform, const std::string& name) {
                EditableSurface surface;
                surface.firstVertex = vertexOffset;
                surface.vertexCount = static_cast<uint32_t>(surfaceVertices.size());
                surface.material = materialOffset;

                Fox::Graphics::Vulkan::Material surfaceMaterial = material;
                surfaceMaterial.layerMaterial = extraMaterials.empty() ? material.layerMaterial : static_cast<int>(materialOffset + 1);
                AddGeneratedSurface(surfaceVertices, surfaceIndices, surfaceMaterial, transform, name, false);
                meshRecords.back().dynamic = true; // Its acceleration structure can be refitted
                surface.record = meshRecords.size() - 1;
                surface.object = placements.size() - 1;

                for (const Fox::Graphics::Vulkan::Material& extra : extraMaterials) {
                    materials.push_back(extra);
                    materialOffset++;
                }

                editableSurfaces.push_back(surface);
                return editableSurfaces.size() - 1;
            }

            size_t RayTracingScene::AddSimulatedSurface(const std::vector<Fox::Graphics::Vulkan::Vertex>& surfaceVertices,
                const std::vector<uint32_t>& surfaceIndices, const Fox::Graphics::Vulkan::Material& material, const std::string& name) {
                // Previous positions (for motion vectors) where the skinned meshes keep theirs: room after the static bind pose
                const uint32_t firstPrevious = static_cast<uint32_t>(bindPose.size());
                for (const Fox::Graphics::Vulkan::Vertex& vertex : surfaceVertices) {
                    SkinVertex placeholder{};
                    std::memcpy(placeholder.position, vertex.position, sizeof(placeholder.position));
                    placeholder.weights[0] = 1.0f;
                    bindPose.push_back(placeholder);
                }

                const size_t surface = AddEditableSurface(surfaceVertices, surfaceIndices, material, {}, glm::mat4(1.0f), name);
                editableSurfaces[surface].firstPrevious = firstPrevious;
                submeshes[meshRecords[editableSurfaces[surface].record].firstSubmesh].previousPositions = firstPrevious + 1;
                return surface;
            }

            RayTracingScene::SimulatedRange RayTracingScene::GetSimulatedRange(size_t surface) const {
                const EditableSurface& s = editableSurfaces[surface];
                return { s.firstVertex, s.vertexCount, s.firstPrevious };
            }

            void RayTracingScene::MarkSurfaceDeformed(size_t surface, bool rebuild) {
                // A refit keeps the tree of boxes and only resizes them: as the surface moves it gets looser,
                // so it is built anew every few seconds
                EditableSurface& s = editableSurfaces[surface];
                if (rebuild || ++s.deformations >= 240) {
                    s.deformations = 0;
                    s.rebuild = true;
                }
                else {
                    s.refit = true;
                }
            }

            void RayTracingScene::UpdateSurfaceVertices(size_t surfaceIndex, uint32_t first, const Fox::Graphics::Vulkan::Vertex* data, uint32_t count) {
                EditableSurface& surface = editableSurfaces[surfaceIndex];
                if (first >= surface.vertexCount) {
                    return;
                }
                count = std::min(count, surface.vertexCount - first);
                std::copy(data, data + count, vertices.begin() + surface.firstVertex + first);
                surface.dirtyBegin = std::min(surface.dirtyBegin, first);
                surface.dirtyEnd = std::max(surface.dirtyEnd, first + count);
                surface.refit = true;
            }

            void RayTracingScene::UpdateSurfacePrevious(size_t surface, const glm::vec4* data, uint32_t count) {
                EditableSurface& s = editableSurfaces[surface];
                if (s.firstPrevious == 0 && submeshes[meshRecords[s.record].firstSubmesh].previousPositions == 0) {
                    return; // Not simulated: no previous positions
                }
                s.previousUpload.assign(data, data + std::min(count, s.vertexCount));
            }

            void RayTracingScene::RebuildSurface(size_t surface) {
                editableSurfaces[surface].rebuild = true;
            }

            void RayTracingScene::SetMaterial(uint32_t index, const Fox::Graphics::Vulkan::Material& material) {
                if (index >= materials.size()) {
                    return;
                }
                materials[index] = material;
                if (std::find(dirtyMaterials.begin(), dirtyMaterials.end(), index) == dirtyMaterials.end()) {
                    dirtyMaterials.push_back(index);
                }
            }

            void RayTracingScene::UpdateTextureRegion(int texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, std::vector<uint8_t> rgba) {
                if (texture < 0 || width == 0 || height == 0 || rgba.size() < static_cast<size_t>(width) * height * 4) {
                    return;
                }
                textureUpdates.push_back({ texture, x, y, width, height, std::move(rgba) });
            }

            void RayTracingScene::RecordEdits(VkCommandBuffer cmd, uint32_t frameSlot, const std::function<void(const char*)>& mark) {
                using Fox::Graphics::Vulkan::Vertex;

                VkDeviceSize stagingBytes = 0;
                bool structures = false;
                for (const EditableSurface& surface : editableSurfaces) {
                    if (surface.dirtyBegin < surface.dirtyEnd) {
                        stagingBytes += sizeof(Vertex) * (surface.dirtyEnd - surface.dirtyBegin);
                    }
                    stagingBytes += sizeof(glm::vec4) * surface.previousUpload.size();
                    structures = structures || surface.refit || surface.rebuild;
                }
                for (const TextureUpdate& update : textureUpdates) {
                    stagingBytes += update.rgba.size();
                }
                if (stagingBytes == 0 && dirtyMaterials.empty() && !structures) {
                    return;
                }

                // --- Uploads: vertices and texels through this frame slot's staging buffer, materials inline ---
                if (stagingBytes > 0 || !dirtyMaterials.empty()) {
                    if (editStaging.size() <= frameSlot) {
                        editStaging.resize(frameSlot + 1);
                        editStagingSize.resize(frameSlot + 1, 0);
                    }
                    if (editStagingSize[frameSlot] < stagingBytes) {
                        const VkDeviceSize size = std::max<VkDeviceSize>(stagingBytes, 4ull << 20);
                        editStaging[frameSlot] = std::make_unique<Fox::Graphics::Vulkan::Buffer>(device, physicalDevice, size,
                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                        editStagingSize[frameSlot] = size;
                    }

                    // The previous frame may still be tracing against these vertices and sampling these textures
                    VkMemoryBarrier beforeCopy{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT };
                    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
                            | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &beforeCopy, 0, nullptr, 0, nullptr);

                    uint8_t* mapped = stagingBytes > 0 ? static_cast<uint8_t*>(editStaging[frameSlot]->Map()) : nullptr;
                    VkDeviceSize at = 0;

                    for (EditableSurface& surface : editableSurfaces) {
                        if (surface.dirtyBegin >= surface.dirtyEnd) {
                            continue;
                        }
                        const VkDeviceSize size = sizeof(Vertex) * (surface.dirtyEnd - surface.dirtyBegin);
                        const VkDeviceSize offset = sizeof(Vertex) * (surface.firstVertex + surface.dirtyBegin);
                        std::memcpy(mapped + at, vertices.data() + surface.firstVertex + surface.dirtyBegin, size);
                        const VkBufferCopy region{ at, offset, size };
                        vkCmdCopyBuffer(cmd, editStaging[frameSlot]->Get(), vertexSSBO->GetBuffer().Get(), 1, &region);
                        vkCmdCopyBuffer(cmd, editStaging[frameSlot]->Get(), vertexBuffers.back()->Get(), 1, &region);
                        at += size;
                        surface.dirtyBegin = UINT32_MAX;
                        surface.dirtyEnd = 0;
                    }
                    for (EditableSurface& surface : editableSurfaces) {
                        if (surface.previousUpload.empty() || !previousPositionsSSBO) {
                            surface.previousUpload.clear();
                            continue;
                        }
                        const VkDeviceSize size = sizeof(glm::vec4) * surface.previousUpload.size();
                        std::memcpy(mapped + at, surface.previousUpload.data(), size);
                        const VkBufferCopy region{ at, sizeof(glm::vec4) * surface.firstPrevious, size };
                        vkCmdCopyBuffer(cmd, editStaging[frameSlot]->Get(), previousPositionsSSBO->GetBuffer().Get(), 1, &region);
                        at += size;
                        surface.previousUpload.clear();
                    }

                    for (uint32_t index : dirtyMaterials) {
                        vkCmdUpdateBuffer(cmd, materialsSSBO->GetBuffer().Get(), sizeof(Fox::Graphics::Vulkan::Material) * index,
                            sizeof(Fox::Graphics::Vulkan::Material), &materials[index]);
                    }
                    dirtyMaterials.clear();

                    const auto& textures = Fox::Graphics::Managers::Vulkan::TextureManager::Get().GetBindlessTextureArray();
                    for (const TextureUpdate& update : textureUpdates) {
                        if (update.texture >= static_cast<int>(textures.size())) {
                            continue;
                        }
                        std::memcpy(mapped + at, update.rgba.data(), update.rgba.size());
                        const VkImage image = textures[update.texture]->GetImage();

                        VkImageMemoryBarrier toTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                        toTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        toTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        toTransfer.image = image;
                        toTransfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

                        VkBufferImageCopy region{};
                        region.bufferOffset = at;
                        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                        region.imageOffset = { static_cast<int32_t>(update.x), static_cast<int32_t>(update.y), 0 };
                        region.imageExtent = { update.width, update.height, 1 };
                        vkCmdCopyBufferToImage(cmd, editStaging[frameSlot]->Get(), image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

                        VkImageMemoryBarrier toShader = toTransfer;
                        toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                        toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                        toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);
                        at += update.rgba.size();
                    }
                    textureUpdates.clear();

                    if (mapped) {
                        editStaging[frameSlot]->Unmap();
                    }

                    // Structure builds and the ray tracing shaders read what was written
                    VkMemoryBarrier afterCopy{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR };
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &afterCopy, 0, nullptr, 0, nullptr);
                }

                if (mark) mark("edits upload");
                // --- Bottom level structures of the edited surfaces: refitted, or built anew ---
                // All in one build command, each with its own part of one scratch buffer, so the GPU builds them side by
                // side. (One after another, behind a barrier each, a few small ones cost milliseconds of waiting: every
                // particle emitter and simulated surface is built anew every frame.)
                struct EditBuild {
                    BottomLevelAccelerationStructure* blas;
                    bool rebuild;
                    VkDeviceSize scratchOffset;
                };
                std::vector<EditBuild> builds;
                {
                    static VkDeviceSize alignment = 0;
                    if (alignment == 0) {
                        VkPhysicalDeviceAccelerationStructurePropertiesKHR properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
                        VkPhysicalDeviceProperties2 properties2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &properties };
                        vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
                        alignment = std::max<VkDeviceSize>(properties.minAccelerationStructureScratchOffsetAlignment, 256);
                    }
                    VkDeviceSize needed = 0;
                    for (EditableSurface& surface : editableSurfaces) {
                        if (!surface.refit && !surface.rebuild) {
                            continue;
                        }
                        const bool rebuild = surface.rebuild;
                        surface.refit = false;
                        surface.rebuild = false;
                        for (int blasIndex : meshRecords[surface.record].blas) {
                            if (blasIndex < 0) {
                                continue;
                            }
                            BottomLevelAccelerationStructure& blas = bottomLevelAccelerationStructure[blasIndex];
                            VkAccelerationStructureBuildGeometryInfoKHR sizeQuery{
                                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                                .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                                .flags = BlasBuildFlags(blas),
                                .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
                                .geometryCount = static_cast<uint32_t>(blas.geometries.size()),
                                .pGeometries = blas.geometries.data()
                            };
                            VkAccelerationStructureBuildSizesInfoKHR sizes{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
                            vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &sizeQuery, blas.primitiveCounts.data(), &sizes);
                            builds.push_back({ &blas, rebuild, needed });
                            const VkDeviceSize scratch = rebuild ? sizes.buildScratchSize : sizes.updateScratchSize;
                            needed += (scratch + alignment - 1) / alignment * alignment;
                        }
                        // The top level structure stores the bounds of the bottom level ones
                        tlasDirty = true;
                    }
                    if (needed > editScratchSize) {
                        if (editScratchBuffer != VK_NULL_HANDLE) {
                            vkDeviceWaitIdle(device); // Frames in flight may still be building with it (rare: it only grows)
                            vkDestroyBuffer(device, editScratchBuffer, nullptr);
                            vkFreeMemory(device, editScratchMemory, nullptr);
                        }
                        // Room to grow (another emitter, a bigger one) without waiting for the GPU again soon
                        const VkDeviceSize size = needed + needed / 2;
                        CreateScratchBuffer(device, physicalDevice, size, editScratchBuffer, editScratchMemory, "Edit BLAS Scratch");
                        editScratchSize = size;
                    }
                }
                if (!builds.empty()) {
                    const VkDeviceAddress scratchAddress = GetBufferAddress(editScratchBuffer);
                    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos;
                    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> ranges;
                    infos.reserve(builds.size());
                    ranges.reserve(builds.size());
                    for (const EditBuild& build : builds) {
                        BottomLevelAccelerationStructure& blas = *build.blas;
                        infos.push_back({
                            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                            .flags = BlasBuildFlags(blas),
                            .mode = build.rebuild ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,
                            .srcAccelerationStructure = build.rebuild ? VK_NULL_HANDLE : blas.handle,
                            .dstAccelerationStructure = blas.handle,
                            .geometryCount = static_cast<uint32_t>(blas.geometries.size()),
                            .pGeometries = blas.geometries.data(),
                            .scratchData = {.deviceAddress = scratchAddress + build.scratchOffset }
                        });
                        ranges.push_back(blas.ranges.data());
                    }

                    // The previous frame's rays and builds must be done with the structures and the scratch memory
                    VkMemoryBarrier beforeBuild{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                        VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                        VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR };
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &beforeBuild, 0, nullptr, 0, nullptr);

                    vkCmdBuildAccelerationStructuresKHR(cmd, static_cast<uint32_t>(infos.size()), infos.data(), ranges.data());

                    VkMemoryBarrier afterBuild{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                        VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                        VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR };
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        0, 1, &afterBuild, 0, nullptr, 0, nullptr);
                }
            }
		            Fox::Graphics::Vulkan::RayTracing::HybridRasterPass::Content RayTracingScene::MakeRasterContent() const {
                using Pass = Fox::Graphics::Vulkan::RayTracing::HybridRasterPass;
                Pass::Content content;

                // A sphere around points: the centre of their box, the farthest from it
                auto sphereOf = [](const std::vector<glm::vec3>& points) {
                    glm::vec3 low(std::numeric_limits<float>::max()), high(-std::numeric_limits<float>::max());
                    for (const glm::vec3& p : points) {
                        low = glm::min(low, p);
                        high = glm::max(high, p);
                    }
                    const glm::vec3 centre = (low + high) * 0.5f;
                    float radius = 0.0f;
                    for (const glm::vec3& p : points) {
                        radius = std::max(radius, glm::distance(p, centre));
                    }
                    return glm::vec4(centre, radius);
                };
                auto merge = [](const glm::vec4& a, const glm::vec4& b) {
                    if (a.w < 0.0f) return b;
                    const glm::vec3 d = glm::vec3(b) - glm::vec3(a);
                    const float distance = glm::length(d);
                    if (distance + b.w <= a.w) return a;
                    if (distance + a.w <= b.w) return b;
                    const float radius = (distance + a.w + b.w) * 0.5f;
                    return glm::vec4(glm::vec3(a) + (distance > 0.0f ? d / distance * (radius - a.w) : glm::vec3(0.0f)), radius);
                };

                // --- Per bottom level structure: its submeshes cut into clusters ---
                struct BlasChunks { uint32_t first = 0, count = 0; glm::vec4 sphere{ 0.0f, 0.0f, 0.0f, -1.0f }; bool bounded = true; };
                std::vector<BlasChunks> blasChunks(blasRanges.size());
                std::vector<glm::vec3> points;
                for (size_t b = 0; b < blasRanges.size(); b++) {
                    const BlasRange& range = blasRanges[b];
                    BlasChunks& out = blasChunks[b];
                    out.first = static_cast<uint32_t>(content.chunks.size());
                    for (uint32_t s = range.firstSubmesh; s < range.firstSubmesh + range.submeshCount && s < submeshes.size(); s++) {
                        const Fox::Graphics::Vulkan::Submesh& submesh = submeshes[s];
                        const uint32_t triangles = submesh.indexCount / 3;
                        const bool masked = submesh.materialIndex < materials.size() && materials[submesh.materialIndex].IsAlphaTested();
                        // Vertices that move (skinned, simulated, sculpted) or not kept on the CPU: no bounds of their own
                        const bool known = !range.dynamic && submesh.indexOffset + submesh.indexCount <= indices.size();
                        for (uint32_t first = 0; first < triangles; first += Pass::CHUNK_TRIANGLES) {
                            Pass::Chunk chunk;
                            chunk.submesh = s;
                            chunk.firstTriangle = first;
                            chunk.triangleCount = std::min(Pass::CHUNK_TRIANGLES, triangles - first);
                            chunk.flags = (masked ? Pass::CHUNK_MASKED : 0u) | (known ? 0u : Pass::CHUNK_UNBOUNDED);
                            if (known) {
                                points.clear();
                                for (uint32_t i = 0; i < chunk.triangleCount * 3; i++) {
                                    const uint32_t index = indices[submesh.indexOffset + first * 3 + i];
                                    if (index < vertices.size()) {
                                        points.emplace_back(vertices[index].position[0], vertices[index].position[1], vertices[index].position[2]);
                                    }
                                }
                                chunk.sphere = points.empty() ? glm::vec4(0.0f) : sphereOf(points);
                                out.sphere = merge(out.sphere, chunk.sphere);
                            }
                            else {
                                out.bounded = false;
                            }
                            content.chunks.push_back(chunk);
                        }
                    }
                    out.count = static_cast<uint32_t>(content.chunks.size()) - out.first;
                }

                // --- Per top level instance ---
                content.instances.resize(instances.size());
                for (size_t i = 0; i < instances.size(); i++) {
                    Pass::RasterInstance& instance = content.instances[i];
                    const uint32_t blas = instanceBlasIndices[i];
                    if (blas == NO_BLAS) {
                        instance.flags = Pass::RASTER_CLUSTER_LOD;
                        continue;
                    }
                    if (blas >= blasChunks.size()) {
                        continue;
                    }
                    instance.firstChunk = blasChunks[blas].first;
                    instance.chunkCount = blasChunks[blas].count;
                    instance.sphere = blasChunks[blas].bounded ? blasChunks[blas].sphere : glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
                }

                // --- The trees' level of detail clusters (a submesh each), by global cluster index ---
                content.clusterChunkBase = static_cast<uint32_t>(content.chunks.size());
                content.chunks.resize(content.chunks.size() + clusterContent.clusters.size());
                for (const MeshRecord& record : meshRecords) {
                    if (record.clusterMesh < 0 || static_cast<size_t>(record.clusterMesh) >= clusterMeshes.size()) {
                        continue;
                    }
                    const ClusterMesh& mesh = clusterMeshes[record.clusterMesh];
                    for (uint32_t j = 0; j < mesh.clusterCount; j++) {
                        const uint32_t cluster = mesh.firstCluster + j;
                        const uint32_t s = record.firstSubmesh + j;
                        if (cluster >= clusterContent.clusters.size() || s >= submeshes.size()) {
                            continue;
                        }
                        Pass::Chunk& chunk = content.chunks[content.clusterChunkBase + cluster];
                        const glm::vec4 bounds = clusterContent.clusters[cluster].selfBounds;
                        // In the bind pose: the wind bends the tree, so a margin
                        chunk.sphere = glm::vec4(glm::vec3(bounds), bounds.w * 1.25f + 0.5f);
                        chunk.submesh = s;
                        chunk.firstTriangle = 0;
                        chunk.triangleCount = submeshes[s].indexCount / 3;
                        chunk.flags = submeshes[s].materialIndex < materials.size() && materials[submeshes[s].materialIndex].IsAlphaTested() ? Pass::CHUNK_MASKED : 0u;
                    }
                }
                return content;
            }

}
	}
}