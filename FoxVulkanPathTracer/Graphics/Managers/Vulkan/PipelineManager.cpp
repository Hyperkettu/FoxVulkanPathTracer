#include "FoxRenderer.h"

// Static member definitions
std::unique_ptr<Fox::Graphics::Managers::Vulkan::PipelineManager> Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::PipelineManager>::instance = nullptr;
std::once_flag Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::PipelineManager>::initFlag;

namespace Fox {

	namespace Graphics {

		namespace Managers {

			namespace Vulkan {

				bool PipelineManager::Initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceCapabilitiesKHR capabilities) {
					{
						VkDescriptorSetLayout descriptorSetLayouts = Fox::Graphics::Managers::Vulkan::DescriptorManager::Get().GetDescriptorSet(Fox::Graphics::Managers::Vulkan::Descriptor::RAY_TRACING)->GetLayout().Get();

						std::unique_ptr<Fox::Graphics::Vulkan::PipelineLayout> rayTracingPipelineLayout = std::make_unique<Fox::Graphics::Vulkan::PipelineLayout>(device,
							std::vector<VkDescriptorSetLayout>{ descriptorSetLayouts },
							std::vector<VkPushConstantRange>{});

						auto builder = std::make_unique<Fox::Graphics::Vulkan::RayTracing::RaytracingPipelineBuilder>(device, physicalDevice);
						builder->AddShader("Shaders/raygen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR)
							.AddShader("Shaders/miss.spv", VK_SHADER_STAGE_MISS_BIT_KHR)
							.AddShader("Shaders/closesthit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
							.SetLayout(rayTracingPipelineLayout->Get())
							.SetMaxRecursionDepth(1);

						rayTracingPipelines[Fox::Graphics::Managers::Vulkan::RayTracingPipelineCategory::MAIN_RAYTRACING_PIPELINE] = std::make_unique<Fox::Graphics::Vulkan::RayTracing::RayTracingPipeline>(std::move(*builder->Build()));
						rayTracingPipelineLayouts[Fox::Graphics::Managers::Vulkan::RayTracingPipelineCategory::MAIN_RAYTRACING_PIPELINE] = std::move(rayTracingPipelineLayout);

						builder->DestroyShaderModules();

					}
						return true;
					
					}

			}
		}
	}
}