#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            namespace RayTracing {

                class RayTracingPipeline {
                public:
                    RayTracingPipeline(
                        VkDevice device,
                        VkPhysicalDevice physicalDevice);

                    ~RayTracingPipeline();

                    void Create(
                        VkPipelineLayout pipelineLayout,
                        const std::vector<VkPipelineShaderStageCreateInfo>& stages,
                        const std::vector<VkRayTracingShaderGroupCreateInfoKHR>& groups,
                        uint32_t maxRecursionDepth = 1);

                    void Bind(VkCommandBuffer cmd) const;

                    static void RegisterExtensionFunctions(VkDevice device) {
                        vkCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR"));
                        vkGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR"));
                        vkGetBufferDeviceAddress = reinterpret_cast<PFN_vkGetBufferDeviceAddress>(vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddress"));
                    }

                    const VkStridedDeviceAddressRegionKHR& GetRaygenRegion() const { return raygenRegion; }
                    const VkStridedDeviceAddressRegionKHR& GetMissRegion() const { return missRegion; }
                    const VkStridedDeviceAddressRegionKHR& GetHitRegion() const { return hitRegion; }
                    const VkStridedDeviceAddressRegionKHR& GetCallableRegion() const { return callableRegion; }

                    VkPipeline GetPipeline() const {
                        return pipeline;
                    }

                private:
                    void CreateSBT(uint32_t groupCount);

                private:
                    VkDevice device;
                    VkPhysicalDevice physicalDevice;

                    VkPipeline pipeline = VK_NULL_HANDLE;

                    VkBuffer sbtBuffer = VK_NULL_HANDLE;
                    VkDeviceMemory sbtMemory = VK_NULL_HANDLE;

                    VkStridedDeviceAddressRegionKHR raygenRegion{};
                    VkStridedDeviceAddressRegionKHR missRegion{};
                    VkStridedDeviceAddressRegionKHR hitRegion{};
                    VkStridedDeviceAddressRegionKHR callableRegion{};

                    static PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;
                    static PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
                    static PFN_vkGetBufferDeviceAddress vkGetBufferDeviceAddress;
                };
            }
        }
    }
}