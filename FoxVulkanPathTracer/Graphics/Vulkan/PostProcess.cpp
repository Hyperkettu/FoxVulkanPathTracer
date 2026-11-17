#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace Managers = Fox::Graphics::Managers::Vulkan;

			namespace {
				// Must match ToneMappingConstants in Shaders/tonemapping_frag.slang
				struct ToneMappingConstants {
					float exposure; // Linear scale
				};

				static_assert(sizeof(ToneMappingConstants) == PostProcessor::PUSH_CONSTANT_SIZE, "Push constant size mismatch");
			}

			PostProcessor::PostProcessor(VkDevice device) : device(device) {
				// Bilinear: denoisers without their own upscaler output at render resolution, which can be below the
				// swapchain resolution. At equal resolution it samples texel centers, so the result is exact.
				VkSamplerCreateInfo samplerInfo{};
				samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
				samplerInfo.magFilter = VK_FILTER_LINEAR;
				samplerInfo.minFilter = VK_FILTER_LINEAR;
				samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
				samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				samplerInfo.maxAnisotropy = 1.0f;

				if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
					throw std::runtime_error("Failed to create the tone mapping sampler!");
				}

				descriptorSet = std::make_unique<DescriptorSet>(
					Managers::DescriptorManager::Get().GetDescriptorSet(Managers::Descriptor::TONE_MAPPING)->AllocateSet());
			}

			PostProcessor::~PostProcessor() {
				// The descriptor set is freed together with its pool (owned by the DescriptorManager).
				if (sampler) {
					vkDestroySampler(device, sampler, nullptr);
				}
			}

			void PostProcessor::SetInput(VkImageView hdrImageView, VkImageLayout layout) {
				descriptorSet->ClearWrites();
				descriptorSet->SetCombinedImageSampler(0, hdrImageView, sampler, layout);
				descriptorSet->Update();
			}

			void PostProcessor::Record(CommandList& commandList, VkFramebuffer framebuffer, VkExtent2D extent,
				const std::function<void(CommandList&)>& overlay) {
				std::array<VkClearValue, 2> clearValues{};
				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
				clearValues[1].depthStencil = { 1.0f, 0 };

				auto& pipelineManager = Managers::PipelineManager::Get();
				VkPipeline pipeline = pipelineManager.GetPipeline(Managers::PipelineCategory::POST_PROCESSOR)->Get();
				VkPipelineLayout pipelineLayout = pipelineManager.GetPipelineLayout(Managers::PipelineCategory::POST_PROCESSOR)->Get();
				VkRenderPass renderPass = Managers::RenderPassManager::Get().GetPass(Managers::RenderPass::DEFAULT)->Get();

				const ToneMappingConstants constants{ std::pow(2.0f, exposureStops) };

				commandList
					.BeginRenderPass(renderPass, framebuffer, extent, clearValues.data(), static_cast<uint32_t>(clearValues.size()))
					.SetViewport(0, 0, extent.width, extent.height)
					.SetScissor(0, 0, extent.width, extent.height)
					.BindPipeline(pipeline)
					.BindDescriptorSets(pipelineLayout, 0, { descriptorSet->Get() })
					.PushConstants(pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(constants), &constants)
					.RenderMeshShader(1, 1, 1);

				if (overlay) {
					overlay(commandList);
				}

				commandList.EndRenderPass();
			}

		}
	}
}
