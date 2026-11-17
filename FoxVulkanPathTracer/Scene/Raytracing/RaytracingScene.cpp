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
                uint32_t submeshCount,
                bool dynamic)
            {
                BottomLevelAccelerationStructure blas{};
                blas.dynamic = dynamic;

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

            namespace {
                // Deforming geometry is refitted every frame instead of rebuilt
                VkBuildAccelerationStructureFlagsKHR BlasBuildFlags(const BottomLevelAccelerationStructure& blas) {
                    return VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                        | (blas.dynamic ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0);
                }
            }

            void RayTracingScene::BuildBLAS(VkCommandBuffer cmd)
            {
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

                vertexOffset += static_cast<uint32_t>(meshVertices.size());
                indexOffset += static_cast<uint32_t>(meshIndices.size());
                materialOffset += static_cast<uint32_t>(meshMaterials.size());

                meshRecords.push_back(record);
                return meshRecords.size() - 1;
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
                for (const Fox::Graphics::Vulkan::Light& light : placement.localLights) {
                    lights.push_back(TransformLight(light, transform));
                }

                placements.push_back(std::move(placement));
            }

            Fox::Graphics::Vulkan::Light RayTracingScene::TransformLight(Fox::Graphics::Vulkan::Light light, const glm::mat4& transform) {
                const glm::mat3 linearTransform(transform);
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

            size_t RayTracingScene::AddSkinnedMesh(const Fox::Graphics::Geometry::SkinnedMesh& mesh, const Fox::Graphics::Vulkan::Material& material,
                const glm::mat4& transform, const std::string& name) {
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
                    bindPose.push_back(skinVertex);
                }

                AddGeneratedSurface(mesh.vertices, mesh.indices, material, transform, name, true);
                meshRecords.back().dynamic = true;
                submeshes.back().previousPositions = skinned.firstSkinVertex + 1;

                skinned.object = placements.size() - 1;
                skinned.record = meshRecords.size() - 1;
                skinnedMeshes.push_back(std::move(skinned));
                return skinnedMeshes.size() - 1;
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

            void RayTracingScene::RecordSkinning(VkCommandBuffer cmd) {
                // The previous frame may still be tracing against these vertices and structures
                VkMemoryBarrier beforeSkinning{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &beforeSkinning, 0, nullptr, 0, nullptr);

                for (const SkinnedRecord& skinned : skinnedMeshes) {
                    skinningPass->Dispatch(cmd, { skinned.firstVertex, skinned.vertexCount, skinned.firstSkinVertex, skinned.firstJoint });
                }

                // The refits read the new vertices, and so do the ray tracing shaders
                VkMemoryBarrier afterSkinning{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                    0, 1, &afterSkinning, 0, nullptr, 0, nullptr);

                // Refit: same triangles, new positions. Much cheaper than a rebuild, and fine for bending.
                for (const SkinnedRecord& skinned : skinnedMeshes) {
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

            void RayTracingScene::RecordFrameUpdate(VkCommandBuffer cmd, uint32_t frameSlot) {
                // Instance motion since the previous frame (for motion vectors), and lights of moved objects.
                // Small, so they are written into the command buffer (vkCmdUpdateBuffer, up to 64 KiB).
                std::vector<InstanceMotion> motion(instancePlacements.size());
                for (Placement& placement : placements) {
                    const glm::mat4 current = placement.transform.Matrix();
                    const glm::mat4 toPrevious = placement.previousMatrix * glm::inverse(current);
                    for (uint32_t instance : placement.instances) {
                        for (int row = 0; row < 3; row++) {
                            motion[instance].rows[row] = glm::vec4(toPrevious[0][row], toPrevious[1][row], toPrevious[2][row], toPrevious[3][row]);
                        }
                    }
                    placement.previousMatrix = current;
                }

                if (lightsDirty) {
                    for (const Placement& placement : placements) {
                        for (size_t i = 0; i < placement.localLights.size(); i++) {
                            lights[placement.firstLight + i] = TransformLight(placement.localLights[i], placement.transform.Matrix());
                        }
                    }
                }

                const VkDeviceSize motionSize = sizeof(InstanceMotion) * motion.size();
                const VkDeviceSize lightsSize = sizeof(Fox::Graphics::Vulkan::Light) * lights.size();
                const bool uploadLights = lightsDirty && lightsSize <= 65536;
                lightsDirty = false;

                // The previous frame may still be reading these buffers
                VkMemoryBarrier beforeWrite{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 1, &beforeWrite, 0, nullptr, 0, nullptr);

                if (motionSize > 0 && motionSize <= 65536) {
                    vkCmdUpdateBuffer(cmd, instanceMotionSSBO->GetBuffer().Get(), 0, motionSize, motion.data());
                }
                if (uploadLights) {
                    vkCmdUpdateBuffer(cmd, lightsSSBO->GetBuffer().Get(), 0, lightsSize, lights.data());
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

                // Deform the skinned meshes every frame (also when their joints did not change: the previous
                // positions must catch up, or a mesh that stopped would keep its last motion vectors)
                if (skinningPass) {
                    RecordSkinning(cmd);
                }

                if (!tlasDirty) {
                    return;
                }
                tlasDirty = false;

                // Rebuild the top level structure in place. It holds one instance per object and surface kind,
                // so a full rebuild costs microseconds and keeps tracing as fast as the original build.
                for (const Placement& placement : placements) {
                    const glm::mat4 transform = placement.transform.Matrix();
                    for (uint32_t instance : placement.instances) {
                        instances[instance].transform = {
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
                    instanceData = std::make_unique<Fox::Graphics::Vulkan::Buffer>(device, physicalDevice, instancesSize,
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                }
                std::memcpy(instanceData->Map(), instances.data(), instancesSize);
                instanceData->Unmap();

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

                vertexOffset += submesh.vertexCount;
                indexOffset += submesh.indexCount;
                materialOffset += 1;
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

                const uint32_t masks[SurfaceKindCount] = { INSTANCE_MASK_OPAQUE, INSTANCE_MASK_GLASS, INSTANCE_MASK_SOLID_GLASS };
                for (size_t p = 0; p < placements.size(); p++) {
                    Placement& placement = placements[p];
                    const MeshRecord& record = meshRecords[placement.record];
                    placement.previousMatrix = placement.transform.Matrix();
                    for (int kind = 0; kind < SurfaceKindCount; kind++) {
                        if (record.blas[kind] >= 0) {
                            placement.instances.push_back(static_cast<uint32_t>(instances.size()));
                            instancePlacements.push_back(static_cast<uint32_t>(p));
                            AddInstance(static_cast<uint32_t>(record.blas[kind]), placement.previousMatrix, record.FirstOf(kind), masks[kind]);
                        }
                    }
                }

                // No motion yet: identity for every instance
                std::vector<InstanceMotion> identity(std::max<size_t>(instances.size(), 1));
                for (InstanceMotion& motion : identity) {
                    motion.rows[0] = glm::vec4(1, 0, 0, 0);
                    motion.rows[1] = glm::vec4(0, 1, 0, 0);
                    motion.rows[2] = glm::vec4(0, 0, 1, 0);
                }
                instanceMotionSSBO = ShaderStorageBuffer<InstanceMotion>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Instance Motion", identity);

                // Skinning: bind pose and joints as inputs, the previous positions start out at the bind pose.
                // The buffers exist (with one unused entry) even without skinned meshes: shaders bind them.
                std::vector<glm::vec4> previous;
                previous.reserve(std::max<size_t>(bindPose.size(), 1));
                for (const SkinVertex& vertex : bindPose) {
                    previous.emplace_back(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f);
                }
                if (previous.empty()) {
                    previous.emplace_back(0.0f);
                }
                previousPositionsSSBO = ShaderStorageBuffer<glm::vec4>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Previous Skinned Positions", previous);

                if (!skinnedMeshes.empty()) {
                    std::vector<JointMatrix> joints;
                    for (const SkinnedRecord& skinned : skinnedMeshes) {
                        joints.insert(joints.end(), skinned.joints.begin(), skinned.joints.end());
                    }

                    bindPoseSSBO = ShaderStorageBuffer<SkinVertex>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Skin Bind Pose", bindPose);
                    jointsSSBO = ShaderStorageBuffer<JointMatrix>::CreateDeviceLocal(device, physicalDevice, pool, queue, "Joint Matrices", joints);
                    skinningPass = std::make_unique<Fox::Graphics::Vulkan::RayTracing::SkinningPass>(device,
                        bindPoseSSBO->GetBufferUnique(), jointsSSBO->GetBufferUnique(), vertexSSBO->GetBufferUnique(),
                        vertexBuffers.back(), previousPositionsSSBO->GetBufferUnique());
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