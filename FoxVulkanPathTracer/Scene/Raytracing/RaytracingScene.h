#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <stdexcept>
#include <glm/glm.hpp>

#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/RaytracingInstance.h"


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

                // Appends a copy of the mesh to the scene. The same mesh can be added several times
                // with different transforms; its textures are shared between the copies.
                void AddMesh(const Fox::Graphics::Geometry::Vulkan::Mesh& mesh, const glm::mat4& transform);

                // Uploads the geometry added so far and builds the acceleration structures.
                // Call once, after the last AddMesh.
                void Build();

                uint32_t GetLightCount() const { return static_cast<uint32_t>(lights.size()); }

                inline std::vector<Fox::Graphics::Vulkan::Vertex>& GetVertices() {
                    return vertices;
                }

                inline std::vector<uint32_t>& GetIndices() {
                    return indices;
                }

                inline std::vector<Fox::Graphics::Vulkan::Submesh>& GetSubmeshes() {
                    return submeshes;
                }

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
                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Vulkan::Submesh>> submeshSSBO;
                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Vulkan::Material>> materialsSSBO; 
                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Vulkan::Light>> lightsSSBO;

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

                std::vector<Fox::Graphics::Vulkan::Vertex> vertices;
                std::vector<uint32_t> indices;  
                std::vector<Fox::Graphics::Vulkan::Submesh> submeshes;
                std::vector<Fox::Graphics::Vulkan::Material> materials;
                std::vector<Fox::Graphics::Vulkan::Light> lights; 
                std::vector<Fox::Graphics::Vulkan::RayTracing::RayTracingInstance> rayTracingInstances;
                std::vector<glm::mat4> transforms;

            private:
                void CreateAccelerationStructure(
                    VkAccelerationStructureTypeKHR type,
                    VkDeviceSize size,
                    VkAccelerationStructureKHR& as,
                    VkBuffer& buffer,
                    VkDeviceMemory& memory);

                void UploadInstanceBuffer();
                void UploadGeometry();
                VkDeviceAddress GetBufferAddress(VkBuffer buffer);

                static PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
                static PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
                static PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
                static PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
                static PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;

                std::vector<std::unique_ptr<Fox::Graphics::Vulkan::VertexBuffer>> vertexBuffers;
                std::vector<std::unique_ptr<Fox::Graphics::Vulkan::IndexBuffer>> indexBuffers;

                // Radiance of the directional light used when a scene defines no lights of its own
                static constexpr float DEFAULT_SUN_RADIANCE = 4.0f;

                // Running offsets into the scene-wide arrays, advanced by AddMesh
                uint32_t vertexOffset = 0u;
                uint32_t indexOffset = 0u; 
                uint32_t materialOffset = 0u;
            };

        } // Raytracing
    } // Scene
} // Fox
