#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <stdexcept>
#include <glm/glm.hpp>

namespace Fox {
    namespace Scene {
        namespace RayTracing {

            class RayTracingScene {
            public:
                RayTracingScene(
                    VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    VkQueue queue,
                    uint32_t queueFamilyIndex);

                ~RayTracingScene();

                virtual void Initialize() = 0;
                virtual void Update(float dt) = 0;

                void Build();

                void AddMesh(Fox::Graphics::Geometry::Vulkan::Mesh& mesh, const glm::mat4& transform);

                inline std::vector<Fox::Graphics::Vulkan::Vertex>& GetVertices() {
                    return vertices;
                }

                inline std::vector<uint32_t>& GetIndices() {
                    return indices;
                }

                inline std::vector<Fox::Graphics::Geometry::Submesh>& GetSubmeshes() {
                    return submeshes;
                }

          /*      inline std::vector<std::vector<Fox::Graphics::Vulkan::Vertex>> GetSubmeshVertices() {
                    return allMeshVertices;
                }

                inline std::vector<std::vector<uint32_t>> GetSubmeshIndices() {
                    return allMeshIndices;
                }

                inline std::vector<Fox::Graphics::Vulkan::MeshGPU>& GetAllMeshGeometries() {
                    return allMeshGeometries;
                }
                */
                void UploadSceneGeometry(VkDevice device, VkPhysicalDevice physDevice, const std::vector<std::vector<Fox::Graphics::Vulkan::Vertex>>& allMeshVertices, const std::vector<std::vector<uint32_t>>& allMeshIndices);

                uint32_t AddBLAS(
                    VkBuffer vertexBuffer,
                    VkDeviceAddress vertexAddress,
                    uint32_t vertexCount,
                    VkBuffer indexBuffer,
                    VkDeviceAddress indexAddress,
                    uint32_t indexCount,
                    uint32_t indexOffset,
                    uint32_t vertexOffset);

                void AddInstance(
                    uint32_t blasIndex,
                    const glm::mat4& transform,
                    uint32_t instanceCustomIndex,
                    uint32_t mask = 0xFF);

                void AddInstance(
                    uint32_t blasIndex,
                    const VkTransformMatrixKHR& transform,
                    uint32_t instanceCustomIndex,
                    uint32_t mask = 0xFF);

                void BuildBLAS(VkCommandBuffer cmd);
                void BuildTLAS(VkCommandBuffer cmd);
                void UpdateTLAS(VkCommandBuffer cmd);

                static void RegisterExtensionFunctions(VkDevice device) {
                    vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));
                    vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));
                    vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR"));
                    vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));
                    vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR"));
                }

                VkAccelerationStructureKHR GetTLAS() const { return topLevelAccelerationStructure.handle; }
                VkDeviceAddress GetTLASAddress() const { return topLevelAccelerationStructure.deviceAddress; }

                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Vulkan::Vertex>> vertexSSBO;
                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<uint32_t>> indexSSBO;
                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Geometry::Submesh>> submeshSSBO;

                static uint32_t FindDeviceLocalMemoryType(
                    VkPhysicalDevice physicalDevice,
                    uint32_t typeFilter);

            protected:
                VkDevice device;
                VkPhysicalDevice physicalDevice;
                VkQueue queue;
                uint32_t queueFamily;

                std::vector<BottomLevelAccelerationStructure> bottomLevelAccelerationStructure;
                TopLevelAccelerationStructure topLevelAccelerationStructure;

                std::vector<VkAccelerationStructureInstanceKHR> instances;
                std::vector<uint32_t> instanceBlasIndices;
                std::unique_ptr<Fox::Graphics::Vulkan::Buffer> instanceBuffer;

                std::unique_ptr<Fox::Graphics::Vulkan::CommandPool> commandPool;


                VkBuffer scratchBuffer = VK_NULL_HANDLE;
                VkDeviceMemory scratchMemory = VK_NULL_HANDLE;

                std::vector<VkBuffer> scratchBuffers;
                std::vector<VkDeviceMemory> scratchBufferMemories;

            //    std::vector<std::vector<Fox::Graphics::Vulkan::Vertex>> allMeshVertices;
            //    std::vector<std::vector<uint32_t>> allMeshIndices;

                std::vector<Fox::Graphics::Vulkan::Vertex> vertices;
                std::vector<uint32_t> indices;  
                std::vector<Fox::Graphics::Geometry::Submesh> submeshes;

                


             //   std::vector<std::unique_ptr<Fox::Graphics::Vulkan::DynamicBuffer<Fox::Graphics::Vulkan::Vertex>>> sceneVertexBuffers;
             //   std::vector<std::unique_ptr<Fox::Graphics::Vulkan::DynamicBuffer<uint32_t>>> sceneIndexBuffers;
             //   std::vector<Fox::Graphics::Vulkan::MeshGPU> allMeshGeometries;

            private:
                void CreateAccelerationStructure(
                    VkAccelerationStructureTypeKHR type,
                    VkDeviceSize size,
                    VkAccelerationStructureKHR& as,
                    VkBuffer& buffer,
                    VkDeviceMemory& memory);

                void UploadInstanceBuffer();
                VkDeviceAddress GetBufferAddress(VkBuffer buffer);

                static PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
                static PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
                static PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
                static PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
                static PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;

                std::vector<std::unique_ptr<Fox::Graphics::Vulkan::VertexBuffer>> vertexBuffers;
                std::vector<std::unique_ptr<Fox::Graphics::Vulkan::IndexBuffer>> indexBuffers;

                uint32_t meshIndex = 0;
            };

        } // Raytracing
    } // Scene
} // Fox
