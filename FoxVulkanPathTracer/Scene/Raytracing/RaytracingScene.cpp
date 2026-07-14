#include "FoxRenderer.h"

extern struct RayTracingInstance;

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
                VkBuffer vertexBuffer,
                VkDeviceAddress vertexAddress,
                uint32_t vertexCount,
                VkBuffer indexBuffer,
                VkDeviceAddress indexAddress,
                uint32_t indexCount,
                uint32_t indexOffset,
                uint32_t vertexOffset)
            {
                BottomLevelAccelerationStructure blas{};

                VkAccelerationStructureGeometryTrianglesDataKHR triangles{
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                    .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                    .vertexData = { vertexAddress },
                    .vertexStride = sizeof(Fox::Graphics::Vulkan::Vertex),
                    .maxVertex = vertexOffset + vertexCount - 1,
                    .indexType = VK_INDEX_TYPE_UINT32,
                    .indexData = { indexAddress }
                };

                VkAccelerationStructureGeometryKHR geometry{
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                    .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
                    .flags = VK_GEOMETRY_OPAQUE_BIT_KHR
                };
                geometry.geometry.triangles = triangles;

                VkAccelerationStructureBuildRangeInfoKHR range{
                    .primitiveCount = indexCount / 3,
                    .primitiveOffset = (indexOffset) * sizeof(uint32_t),
                    .firstVertex = 0,
                    .transformOffset = 0
                };

                blas.geometries.push_back(geometry);
                blas.ranges.push_back(range);

                blas.primitiveCounts.push_back(range.primitiveCount);

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

            void RayTracingScene::AddMesh(std::unique_ptr<Fox::Graphics::Geometry::Vulkan::Mesh>& mesh, glm::mat4& transform) {

                auto meshVertices = mesh->GetVertices();
                auto meshIndices = mesh->GetIndices();
                auto meshSubmeshes = mesh->GetSubmeshesForGPU();
                auto meshMaterials = mesh->GetMaterials();
                auto meshLights = mesh->GetLights();

                for (auto i = 0; i < meshVertices.size(); i++) {
                    vertices.push_back(meshVertices[i]);
                }

                for (auto i = 0; i < meshIndices.size(); i++) {
                    indices.push_back(meshIndices[i]);
                }

                for (auto i = 0; i < meshSubmeshes.size(); i++) {
                    submeshes.push_back(meshSubmeshes[i]);
                }
                 
                for (auto i = 0; i < meshMaterials.size(); i++) { 
                    materials.push_back(meshMaterials[i]); 
                }

                for (auto i = 0; i < meshLights.size(); i++) {
                    lights.push_back(meshLights[i]);
                }

                auto vertexBuffer = std::make_unique<Fox::Graphics::Vulkan::VertexBuffer>();
                vertexBuffer->Create(device, physicalDevice, commandPool->Get(), queue, vertices, "Raytracing Vertex Buffer");
                vertexBuffers.push_back(std::move(vertexBuffer));

                auto indexBuffer = std::make_unique<Fox::Graphics::Vulkan::IndexBuffer>();
                indexBuffer->Create(device, physicalDevice, commandPool->Get(), queue, indices, "Raytracing Index Buffer");
                indexBuffers.push_back(std::move(indexBuffer));


                vertexSSBO = std::make_unique<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Vulkan::Vertex>>(device, physicalDevice, "Scene Vertex SSBO", vertices);
                vertexSSBO->Update(vertices);
 
                indexSSBO = std::make_unique<Fox::Graphics::Vulkan::ShaderStorageBuffer<uint32_t>>(device, physicalDevice, "Mesh Index SSBO ", indices); 
                indexSSBO->Update(indices);

                submeshSSBO = std::make_unique<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Vulkan::Submesh>>(device, physicalDevice, "Mesh Submesh SSBO ", submeshes);
                submeshSSBO->Update(submeshes);

                materialsSSBO = std::make_unique<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Vulkan::Material>>(device, physicalDevice, "Scene Materials", materials);
                materialsSSBO->Update(materials); 

                lightsSSBO = std::make_unique<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Vulkan::Light>>(device, physicalDevice, "Scene Lights", lights);
                lightsSSBO->Update(lights); 

                VkDeviceAddress vertexAddress = Fox::Graphics::Vulkan::GetBufferDeviceAddress(device, vertexBuffers.back()->Get());
                VkDeviceAddress indexAddress = Fox::Graphics::Vulkan::GetBufferDeviceAddress(device, indexBuffers.back()->Get());

                auto instances = mesh->GetInstanceData();
                size_t elementCount = submeshes.size(); 

                for (size_t i = 0; i < elementCount; i++) {
                    const auto& subMesh = submeshes[i];
                    const auto& instance = instances[i];
                    glm::mat4 matrix = transform * instance.transform;

                    auto blasIndex = AddBLAS(
                        vertexBuffers.back()->Get(),
                        vertexAddress,
                        subMesh.vertexCount,     
                        indexBuffers.back()->Get() ,
                        indexAddress,
                        subMesh.indexCount,   
                        subMesh.indexOffset,     
                        subMesh.vertexOffset      
                    );

                    AddInstance(blasIndex, matrix, instance.materialIndex, 0xFF);
                }
            }

            void RayTracingScene::Build() {
                Fox::Graphics::Vulkan::CommandList cmdList(device, commandPool->Get());
                    cmdList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
                        .BuildAccelerationStructures(*this)
                        .End()
                        .SubmitAndWait(queue);
            }
		}
	}
}