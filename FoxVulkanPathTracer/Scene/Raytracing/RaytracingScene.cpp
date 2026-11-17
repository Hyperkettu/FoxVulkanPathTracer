#include "FoxRenderer.h"

namespace Fox {

	namespace Scene {

		namespace RayTracing {

            PFN_vkCreateAccelerationStructureKHR Fox::Scene::RayTracing::RayTracingScene::vkCreateAccelerationStructureKHR;
            PFN_vkDestroyAccelerationStructureKHR Fox::Scene::RayTracing::RayTracingScene::vkDestroyAccelerationStructureKHR;
            PFN_vkCmdBuildAccelerationStructuresKHR Fox::Scene::RayTracing::RayTracingScene::vkCmdBuildAccelerationStructuresKHR;
            PFN_vkGetAccelerationStructureDeviceAddressKHR Fox::Scene::RayTracing::RayTracingScene::vkGetAccelerationStructureDeviceAddressKHR;
            PFN_vkGetAccelerationStructureBuildSizesKHR Fox::Scene::RayTracing::RayTracingScene::vkGetAccelerationStructureBuildSizesKHR;

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

                vertexBuffers.clear();
                indexBuffers.clear();
            }
		
            uint32_t RayTracingScene::AddBLAS(
                VkDeviceAddress vertexAddress,
                VkDeviceAddress indexAddress,
                uint32_t firstSubmesh,
                uint32_t submeshCount)
            {
                BottomLevelAccelerationStructure blas{};

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

                    VkAccelerationStructureGeometryKHR geometry{
                        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
                        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR
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
            }

            void RayTracingScene::BuildBLAS(VkCommandBuffer cmd)
            {
                VkDeviceSize maxScratchSize = 0;

                for (auto& blas : bottomLevelAccelerationStructure)
                {
                    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
                        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
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

                    if (sizeInfo.buildScratchSize > maxScratchSize) {
                        maxScratchSize = sizeInfo.buildScratchSize;
                    }

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

                scratchBuffers.push_back(globalScratchBuffer);
                scratchBufferMemories.push_back(globalScratchMemory); 

                for (auto& blas : bottomLevelAccelerationStructure)
                {
                    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
                        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
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
                    instances[i].accelerationStructureReference = bottomLevelAccelerationStructure[instanceBlasIndices[i]].deviceAddress;
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

                vkGetAccelerationStructureBuildSizesKHR(
                    device,
                    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                    &buildInfo,
                    &instanceCount,
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

            void RayTracingScene::UpdateTLAS(VkCommandBuffer cmd)
            {
                UploadInstanceBuffer();

                VkDeviceAddress instanceAddress =
                    GetBufferAddress(instanceBuffer->Get());

                VkAccelerationStructureGeometryInstancesDataKHR instances{
                    .sType =
                        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                    .data = { instanceAddress },
                };

                instances.arrayOfPointers = VK_FALSE;

                VkAccelerationStructureGeometryKHR geometry{
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                    .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
                    .geometry = {.instances = instances }
                };

                VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                    .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
                    .flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
                    .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,
                    .srcAccelerationStructure = topLevelAccelerationStructure.handle,
                    .dstAccelerationStructure = topLevelAccelerationStructure.handle,
                    .geometryCount = 1,
                    .pGeometries = &geometry,
                    .scratchData = {
                        .deviceAddress = GetBufferAddress(scratchBuffer)
                    }
                };

                VkAccelerationStructureBuildRangeInfoKHR range{
                    .primitiveCount = uint32_t(this->instances.size())
                };

                const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { &range };

                vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, ranges);
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
                VkDeviceSize size =
                    sizeof(VkAccelerationStructureInstanceKHR) * instances.size();

                instanceBuffer = std::make_unique<Fox::Graphics::Vulkan::Buffer>(
                    device,
                    physicalDevice,
                    size,
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                void* mapped = instanceBuffer->Map();
                memcpy(mapped, instances.data(), size);
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

                // Opaque submeshes first, then glass: each group becomes one BLAS
                auto isGlass = [&](const Fox::Graphics::Vulkan::Submesh& submesh) {
                    return meshMaterials[submesh.materialIndex - materialOffset].transmission > 0.5f;
                };
                for (const auto& submesh : rebased) {
                    if (!isGlass(submesh)) { submeshes.push_back(submesh); record.opaqueCount++; }
                }
                for (const auto& submesh : rebased) {
                    if (isGlass(submesh)) { submeshes.push_back(submesh); record.glassCount++; }
                }

                // Materials refer to the lights of the mesh's first placement (added right after this)
                const int lightOffset = static_cast<int>(lights.size());
                for (Fox::Graphics::Vulkan::Material material : meshMaterials) {
                    if (material.lightIndex >= 0) {
                        material.lightIndex += lightOffset;
                    }
                    materials.push_back(material);
                }

                vertexOffset += static_cast<uint32_t>(meshVertices.size());
                indexOffset += static_cast<uint32_t>(meshIndices.size());
                materialOffset += static_cast<uint32_t>(meshMaterials.size());

                meshRecords.push_back(record);
                return meshRecords.size() - 1;
            }

            void RayTracingScene::AddMesh(const Fox::Graphics::Geometry::Vulkan::Mesh& mesh, const glm::mat4& transform) {
                // Geometry is stored once per mesh and shared by all its placements
                auto existing = std::find_if(meshRecords.begin(), meshRecords.end(),
                    [&mesh](const MeshRecord& record) { return record.mesh == &mesh; });

                const size_t record = existing != meshRecords.end()
                    ? static_cast<size_t>(existing - meshRecords.begin())
                    : AddMeshGeometry(&mesh);

                placements.push_back({ record, transform });

                // Lights are stored in world space
                const glm::mat3 linearTransform(transform);
                for (Fox::Graphics::Vulkan::Light light : mesh.GetLights()) {
                    if (light.type == (int)Fox::Core::Loaders::GLTF::LightType::QuadAreaLight) {
                        light.position = glm::vec3(transform * glm::vec4(light.position, 1.0f));
                        light.tangentX = linearTransform * light.tangentX;
                        light.tangentY = linearTransform * light.tangentY;
                        // The tangents are half extents
                        light.area = glm::length(glm::cross(2.0f * light.tangentX, 2.0f * light.tangentY));
                    }

                    light.normal = glm::normalize(linearTransform * light.normal);
                    lights.push_back(light);
                }
            }

            void RayTracingScene::AddGroundPlane(float height, float halfSize, const Fox::Graphics::Vulkan::Material& material) {
                const float corners[4][2] = { { -1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, -1.0f } };

                for (const auto& corner : corners) {
                    Fox::Graphics::Vulkan::Vertex vertex{};
                    vertex.position[0] = corner[0] * halfSize;
                    vertex.position[1] = height;
                    vertex.position[2] = corner[1] * halfSize;
                    vertex.normal[1] = 1.0f;
                    vertex.uv[0] = corner[0] * 0.5f + 0.5f;
                    vertex.uv[1] = corner[1] * 0.5f + 0.5f;
                    vertices.push_back(vertex);
                }

                for (uint32_t index : { 0u, 1u, 2u, 0u, 2u, 3u }) {
                    indices.push_back(vertexOffset + index);
                }

                Fox::Graphics::Vulkan::Submesh submesh{};
                submesh.vertexOffset = vertexOffset;
                submesh.indexOffset = indexOffset;
                submesh.vertexCount = 4;
                submesh.indexCount = 6;
                submesh.materialIndex = materialOffset;

                MeshRecord record;
                record.firstSubmesh = static_cast<uint32_t>(submeshes.size());
                record.opaqueCount = 1;
                meshRecords.push_back(record);
                placements.push_back({ meshRecords.size() - 1, glm::mat4(1.0f) });

                submeshes.push_back(submesh);

                Fox::Graphics::Vulkan::Material groundMaterial = material;
                groundMaterial.lightIndex = -1;
                materials.push_back(groundMaterial);

                vertexOffset += 4;
                indexOffset += 6;
                materialOffset += 1;
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

            void RayTracingScene::UploadGeometry() {
                // The scene is always lit by the environment map; analytic lights are optional.
                lightCount = static_cast<uint32_t>(lights.size());

                // A storage buffer cannot be empty, so keep one unused entry around.
                if (lights.empty()) {
                    lights.push_back(Fox::Graphics::Vulkan::Light{});
                }

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
                submeshSSBO = ShaderStorageBuffer<Fox::Graphics::Vulkan::Submesh>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Scene Submesh SSBO", submeshes);
                materialsSSBO = ShaderStorageBuffer<Fox::Graphics::Vulkan::Material>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Scene Materials", materials);
                lightsSSBO = ShaderStorageBuffer<Fox::Graphics::Vulkan::Light>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Scene Lights", lights);

                VkDeviceAddress vertexAddress = Fox::Graphics::Vulkan::GetBufferDeviceAddress(device, vertexBuffers.back()->Get());
                VkDeviceAddress indexAddress = Fox::Graphics::Vulkan::GetBufferDeviceAddress(device, indexBuffers.back()->Get());

                // One BLAS per mesh for its opaque surfaces and one for its glass, shared by all placements of
                // the mesh. Each submesh is one geometry of its BLAS: the closest hit shader finds the submesh as
                // InstanceID() + GeometryIndex(), so an instance's custom index is the index of its first submesh.
                for (MeshRecord& record : meshRecords) {
                    if (record.opaqueCount > 0) {
                        record.opaqueBlas = static_cast<int>(AddBLAS(vertexAddress, indexAddress, record.firstSubmesh, record.opaqueCount));
                    }
                    if (record.glassCount > 0) {
                        record.glassBlas = static_cast<int>(AddBLAS(vertexAddress, indexAddress, record.firstSubmesh + record.opaqueCount, record.glassCount));
                    }
                }

                for (const Placement& placement : placements) {
                    const MeshRecord& record = meshRecords[placement.record];
                    if (record.opaqueBlas >= 0) {
                        AddInstance(static_cast<uint32_t>(record.opaqueBlas), placement.transform, record.firstSubmesh, INSTANCE_MASK_OPAQUE);
                    }
                    if (record.glassBlas >= 0) {
                        AddInstance(static_cast<uint32_t>(record.glassBlas), placement.transform, record.firstSubmesh + record.opaqueCount, INSTANCE_MASK_GLASS);
                    }
                }
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
		}
	}
}