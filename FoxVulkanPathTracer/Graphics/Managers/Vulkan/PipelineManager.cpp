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
						VkDescriptorSetLayout bindlessTextureLayout = Fox::Graphics::Managers::Vulkan::TextureManager::Get().bindlessTextureDescriptorSet->GetLayout();

						std::unique_ptr<Fox::Graphics::Vulkan::PipelineLayout> rayTracingPipelineLayout = std::make_unique<Fox::Graphics::Vulkan::PipelineLayout>(device,
							std::vector<VkDescriptorSetLayout>{ descriptorSetLayouts, bindlessTextureLayout },
							std::vector<VkPushConstantRange>{});

						// The shader binding table layout in RayTracingPipeline::CreateSBT depends on this order.
						Fox::Graphics::Vulkan::RayTracing::RaytracingPipelineBuilder builder(device, physicalDevice);
						builder.AddShader("Shaders/raygen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR)
							.AddShader("Shaders/miss.spv", VK_SHADER_STAGE_MISS_BIT_KHR)
							.AddShader("Shaders/closest_hit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
							.AddShader("Shaders/shadowmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR)
							.SetLayout(rayTracingPipelineLayout->Get())
							.SetMaxRecursionDepth(MAX_RAY_RECURSION_DEPTH);

						rayTracingPipelines[Fox::Graphics::Managers::Vulkan::RayTracingPipelineCategory::MAIN_RAYTRACING_PIPELINE] = builder.Build();
						rayTracingPipelineLayouts[Fox::Graphics::Managers::Vulkan::RayTracingPipelineCategory::MAIN_RAYTRACING_PIPELINE] = std::move(rayTracingPipelineLayout);

						builder.DestroyShaderModules();

					}

					{
						// Tone mapping: a mesh shader emits a full-screen quad, the fragment shader maps HDR -> LDR.
						VkDescriptorSetLayout toneMappingLayout = Fox::Graphics::Managers::Vulkan::DescriptorManager::Get().GetDescriptorSet(Fox::Graphics::Managers::Vulkan::Descriptor::TONE_MAPPING)->GetLayout().Get();

						std::unique_ptr<Fox::Graphics::Vulkan::PipelineLayout> postProcessorPipelineLayout = std::make_unique<Fox::Graphics::Vulkan::PipelineLayout>(device,
							std::vector<VkDescriptorSetLayout>{ toneMappingLayout },
							std::vector<VkPushConstantRange>{});

						std::unique_ptr<Fox::Graphics::Vulkan::Pipeline> postProcessorPipeline = std::make_unique<Fox::Graphics::Vulkan::Pipeline>(Fox::Graphics::Vulkan::PipelineBuilder(device)
							.WithShader(Fox::Graphics::Enums::PipelineShaderStage::MESH_SHADER, "Shaders/tonemapping_mesh.spv")
							.WithShader(Fox::Graphics::Enums::PipelineShaderStage::FRAGMENT_SHADER, "Shaders/tonemapping_frag.spv")
							.WithRasterization(VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, 1.0f, VK_CULL_MODE_NONE)
							.WithDepthStencil(VK_FALSE, VK_FALSE)
							.WithViewport(0.0f, 0.0f, static_cast<float>(capabilities.currentExtent.width), static_cast<float>(capabilities.currentExtent.height), 0.0f, 1.0f)
							.WithScissor(0, 0, capabilities.currentExtent.width, capabilities.currentExtent.height)
							.SetLayout(postProcessorPipelineLayout->Get())
							.SetRenderPass(Fox::Graphics::Managers::Vulkan::RenderPassManager::Get().GetPass(Fox::Graphics::Managers::Vulkan::RenderPass::DEFAULT)->Get())
							.Build());

						pipelines[Fox::Graphics::Managers::Vulkan::PipelineCategory::POST_PROCESSOR] = std::move(postProcessorPipeline);
						pipelineLayouts[Fox::Graphics::Managers::Vulkan::PipelineCategory::POST_PROCESSOR] = std::move(postProcessorPipelineLayout);

					}

						return true;
					
					}

			}
		}
	}
}