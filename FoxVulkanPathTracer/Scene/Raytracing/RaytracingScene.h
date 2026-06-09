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

                uint32_t AddBLAS(
                    VkBuffer vertexBuffer,
                    VkDeviceAddress vertexAddress,
                    uint32_t vertexCount,
                    VkBuffer indexBuffer,
                    VkDeviceAddress indexAddress,
                    uint32_t indexCount);

                void AddInstance(
                    uint32_t blasIndex,
                    const glm::mat4& transform,
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

                static uint32_t FindDeviceLocalMemoryType(
                    VkPhysicalDevice physicalDevice,
                    uint32_t typeFilter);

            private:
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
            };

        } // Raytracing
    } // Scene
} // Fox
