#include "FoxRenderer.h"
#include <cassert>
#include <cstring>



namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            namespace RayTracing {

                PFN_vkCreateRayTracingPipelinesKHR RayTracingPipeline::vkCreateRayTracingPipelinesKHR;
                PFN_vkGetRayTracingShaderGroupHandlesKHR RayTracingPipeline::vkGetRayTracingShaderGroupHandlesKHR;
                PFN_vkGetBufferDeviceAddress RayTracingPipeline::vkGetBufferDeviceAddress;

                static uint32_t Align(uint32_t value, uint32_t alignment)
                {
                    return (value + alignment - 1) & ~(alignment - 1);
                }

                RayTracingPipeline::RayTracingPipeline(
                    VkDevice device,
                    VkPhysicalDevice physicalDevice)
                    : device(device)
                    , physicalDevice(physicalDevice)
                {
                }

                RayTracingPipeline::~RayTracingPipeline()
                {
                    if (pipeline) {
                        vkDestroyPipeline(device, pipeline, nullptr);
                    }

                    if (sbtBuffer) {
                        vkDestroyBuffer(device, sbtBuffer, nullptr);
                    }

                    if (sbtMemory) {
                        vkFreeMemory(device, sbtMemory, nullptr);
                    }
                }

                void RayTracingPipeline::Create(
                    VkPipelineLayout pipelineLayout,
                    const std::vector<VkPipelineShaderStageCreateInfo>& stages,
                    const std::vector<VkRayTracingShaderGroupCreateInfoKHR>& groups,
                    uint32_t maxRecursionDepth)
                {
                    VkRayTracingPipelineCreateInfoKHR info{
                        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
                        .stageCount = uint32_t(stages.size()),
                        .pStages = stages.data(),
                        .groupCount = uint32_t(groups.size()),
                        .pGroups = groups.data(),
                        .maxPipelineRayRecursionDepth = maxRecursionDepth,
                        .layout = pipelineLayout
                    };

                    VkResult res = vkCreateRayTracingPipelinesKHR(
                        device,
                        VK_NULL_HANDLE,
                        VK_NULL_HANDLE,
                        1,
                        &info,
                        nullptr,
                        &pipeline);

                    assert(res == VK_SUCCESS);

                    CreateSBT(uint32_t(groups.size()));
                }

                void RayTracingPipeline::Bind(VkCommandBuffer cmd) const
                {
                    vkCmdBindPipeline(
                        cmd,
                        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                        pipeline);
                }

                void RayTracingPipeline::CreateSBT(uint32_t groupCount)
                {
                    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{
                        .sType =
                            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
                    };

                    VkPhysicalDeviceProperties2 props2{
                        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                        .pNext = &rtProps
                    };

                    vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

                    uint32_t handleSize = rtProps.shaderGroupHandleSize;
                    uint32_t handleAlign = rtProps.shaderGroupBaseAlignment;

                    uint32_t raygenCount = 1;
                    uint32_t missCount = 1;
                    uint32_t hitCount = groupCount - raygenCount - missCount;

                    uint32_t raygenSize = Align(handleSize, handleAlign);
                    uint32_t missSize = Align(handleSize * missCount, handleAlign);
                    uint32_t hitSize = Align(handleSize * hitCount, handleAlign);

                    uint32_t sbtSize = raygenSize + missSize + hitSize;

                    // Create SBT buffer
                    VkBufferCreateInfo bufferInfo{
                        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                        .size = sbtSize,
                        .usage =
                            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
                    };

                    vkCreateBuffer(device, &bufferInfo, nullptr, &sbtBuffer);

                    VkMemoryRequirements memReq;
                    vkGetBufferMemoryRequirements(device, sbtBuffer, &memReq);

                    VkMemoryAllocateFlagsInfo flags{
                        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
                        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
                    };

                    VkMemoryAllocateInfo allocInfo{
                        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                        .pNext = &flags,
                        .allocationSize = memReq.size,
                        .memoryTypeIndex = Fox::Graphics::Vulkan::Buffer::FindMemoryType(
                            physicalDevice,
                            memReq.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                    };

                    vkAllocateMemory(device, &allocInfo, nullptr, &sbtMemory);
                    vkBindBufferMemory(device, sbtBuffer, sbtMemory, 0);

                    // Get handles
                    std::vector<uint8_t> handles(groupCount * handleSize);
                    vkGetRayTracingShaderGroupHandlesKHR(
                        device,
                        pipeline,
                        0,
                        groupCount,
                        handles.size(),
                        handles.data());

                    // Copy to SBT
                    void* mapped;
                    vkMapMemory(device, sbtMemory, 0, VK_WHOLE_SIZE, 0, &mapped);

                    uint8_t* p = static_cast<uint8_t*>(mapped);
                    memcpy(p, handles.data(), handleSize);                  // raygen
                    memcpy(p + raygenSize, handles.data() + handleSize, handleSize); // miss
                    memcpy(p + raygenSize + missSize,
                        handles.data() + handleSize * 2,
                        handleSize * hitCount);                           // hit

                    vkUnmapMemory(device, sbtMemory);

                    VkBufferDeviceAddressInfo deviceAddressInfo {
                        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                        nullptr,
                        sbtBuffer
                    };

                    VkDeviceAddress baseAddress =
                        vkGetBufferDeviceAddress(device,
                            &deviceAddressInfo);

                    raygenRegion = { baseAddress, raygenSize, raygenSize };
                    missRegion = { baseAddress + raygenSize, handleSize, missSize };
                    hitRegion = { baseAddress + raygenSize + missSize, handleSize, hitSize };
                    callableRegion = {};
                }
            }
        }
    }
}