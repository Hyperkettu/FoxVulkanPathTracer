#include "FoxRenderer.h"

namespace Fox
{
    namespace Graphics
    {
        namespace Vulkan
        {
            namespace RayTracing {

                Fox::Graphics::Vulkan::RayTracing::RaytracingPipelineBuilder& Fox::Graphics::Vulkan::RayTracing::RaytracingPipelineBuilder::AddShader(const std::string& filePath, VkShaderStageFlagBits stage) {
                    std::vector<char> shaderCode = Fox::Core::FileSystem::ReadBinaryFile(filePath.c_str());
                    VkShaderModule shaderModule = CreateShaderModule(device, reinterpret_cast<uint32_t*>(shaderCode.data()), shaderCode.size());
					shaderModules.push_back(shaderModule);

                    shaderStages.push_back({
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = stage,
                        .pName = "main",
						});

					shaderStages.back().module = shaderModule;	

					VkRayTracingShaderGroupCreateInfoKHR group{};
					group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;

					if (stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR) {
						group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
						group.generalShader = shaderStages.size() - 1;
						group.closestHitShader = VK_SHADER_UNUSED_KHR;
						group.anyHitShader = VK_SHADER_UNUSED_KHR;
						group.intersectionShader = VK_SHADER_UNUSED_KHR;
					}
					else if (stage == VK_SHADER_STAGE_MISS_BIT_KHR) {
						group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
						group.generalShader = shaderStages.size() - 1;
						group.closestHitShader = VK_SHADER_UNUSED_KHR;
						group.anyHitShader = VK_SHADER_UNUSED_KHR;
						group.intersectionShader = VK_SHADER_UNUSED_KHR;
					}
					else if (stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) {
						group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
						group.closestHitShader = shaderStages.size() - 1;
						group.generalShader = VK_SHADER_UNUSED_KHR;
						group.anyHitShader = VK_SHADER_UNUSED_KHR;
						group.intersectionShader = VK_SHADER_UNUSED_KHR;
					}

					shaderGroups.push_back(group);

                    return *this;
                }

				Fox::Graphics::Vulkan::RayTracing::RayTracingPipeline* Fox::Graphics::Vulkan::RayTracing::RaytracingPipelineBuilder::Build() const {
					return new Fox::Graphics::Vulkan::RayTracing::RayTracingPipeline(device, physicalDevice, pipelineLayout, shaderStages, shaderGroups, maxRecursionDepth);
				}
            }
        } 
    }
}