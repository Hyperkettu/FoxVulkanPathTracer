#pragma once

namespace Fox
{
    namespace Graphics
    {
        namespace Vulkan
        {
            namespace RayTracing
            {

                class RaytracingPipelineBuilder {
                public:
                    explicit RaytracingPipelineBuilder(VkDevice device, VkPhysicalDevice physicalDevice)
                        : device(device), physicalDevice(physicalDevice) {
                    }

                    RaytracingPipelineBuilder& AddShader(const std::string& filePath, VkShaderStageFlagBits stage);

                    RaytracingPipelineBuilder& SetLayout(VkPipelineLayout layout) {
                        pipelineLayout = layout;
                        return *this;
                    }

                    RaytracingPipelineBuilder& SetMaxRecursionDepth(uint32_t maxDepth) {
                        maxRecursionDepth = maxDepth;
                        return *this;
					}


                    Fox::Graphics::Vulkan::RayTracing::RayTracingPipeline* Build() const;

                    void DestroyShaderModules() {
                        for (VkShaderModule module : shaderModules) {
                            vkDestroyShaderModule(device, module, nullptr);
                        }
                        shaderModules.clear();
                    }

                private:

                    VkShaderModule CreateShaderModule(
                        VkDevice device,
                        uint32_t* spirv,
                        size_t size
                    )
                    {
                        VkShaderModuleCreateInfo info{
                            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                            .codeSize = size,
                            .pCode = spirv
                        };

                        VkShaderModule module;
                        vkCreateShaderModule(device, &info, nullptr, &module);
                        return module;
                    }

                    VkDevice device;
                    VkPhysicalDevice physicalDevice;
                    VkRenderPass renderPass = VK_NULL_HANDLE;
                    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

                    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
					std::vector<VkShaderModule> shaderModules;
					std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
					uint32_t maxRecursionDepth = 1;

                };
            }
        } 
    }
}