#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			void PostProcessor::Process(Fox::Graphics::Vulkan::CommandList& commandList, VkSurfaceCapabilitiesKHR capabilities, VkQueue graphicsQueue, VkSemaphore renderFinishedSemaphore, std::unique_ptr<Fox::Graphics::Vulkan::Swapchain>& swapchain, uint32_t imageIndex) {

				std::array<VkClearValue, 2> clearValues{};
				clearValues[0].color = { 0.0f, 0.0f, 0.0f, 0.0f };
				clearValues[1].depthStencil = { 1.0f, 0 };

				VkImageSubresourceRange subresourceRange = { 
					VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
				};

				auto postProcessingPipeline = Fox::Graphics::Managers::Vulkan::PipelineManager::Get().GetPipeline(Fox::Graphics::Managers::Vulkan::PipelineCategory::POST_PROCESSOR)->Get();
				auto postProcessingPipelineLayout = Fox::Graphics::Managers::Vulkan::PipelineManager::Get().GetPipelineLayout(Fox::Graphics::Managers::Vulkan::PipelineCategory::POST_PROCESSOR)->Get();

				VkFramebuffer frameBuffer = nullptr;

				commandList.Begin()
					.BeginRenderPass(Fox::Graphics::Managers::Vulkan::RenderPassManager::Get().GetPass(Fox::Graphics::Managers::Vulkan::RenderPass::DEFAULT)->Get(),
						frameBuffer, capabilities.currentExtent, &clearValues[0], 2)
					.SetViewport(0, 0, capabilities.currentExtent.width, capabilities.currentExtent.height)
					.SetScissor(0, 0, capabilities.currentExtent.width, capabilities.currentExtent.height)
					.BindPipeline(postProcessingPipeline)
					.BindDescriptorSets(postProcessingPipelineLayout, 0, {})
					.RenderMeshShader(1, 1, 1)
					.EndRenderPass()
				/*	.TransitionImageLayout(
						swapchain->GetImage(imageIndex),
						VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
						subresourceRange,
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT,
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)

					.TransitionImageLayout(
					//	frameResource->storageTexture->GetImage(),
						Fox::Graphics::Managers::Vulkan::TextureManager::Get().GetRenderTargetTexture(Fox::Graphics::Managers::Vulkan::RenderTargetTexture::DEFAULT_HALF_FLOATING_POINT_RENDER_TARGET)->GetImage(),
						VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						VkImageLayout::VK_IMAGE_LAYOUT_GENERAL,
						subresourceRange,
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT,
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR)*/ 
					.End();

				
				commandList.Submit(graphicsQueue, VK_NULL_HANDLE, renderFinishedSemaphore, VK_NULL_HANDLE);
			}


		}
	}
}