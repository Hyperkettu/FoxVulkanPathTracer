#include "FoxRenderer.h"

// Static member definitions
std::unique_ptr<Fox::Graphics::Managers::Vulkan::RenderPassManager> Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::RenderPassManager>::instance = nullptr;
std::once_flag Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::RenderPassManager>::initFlag;

namespace Fox {

	namespace Graphics {

		namespace Managers {

			namespace Vulkan {

				bool RenderPassManager::Initialize(VkDevice device, VkSurfaceCapabilitiesKHR capabilities, VkSurfaceFormatKHR surfaceFormat, VkFormat depthFormat) {

					auto builder = std::make_unique<Fox::Graphics::Vulkan::RenderPassBuilder>(device);
					{
						uint32_t color = builder->AddColorAttachment(surfaceFormat.format,
							VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR,
							VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE,
							VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
						uint32_t depth = builder->AddDepthAttachment(depthFormat);

						std::unique_ptr<Fox::Graphics::Vulkan::RenderPass> offscreenRenderPass = std::make_unique<Fox::Graphics::Vulkan::RenderPass>(builder
							->BeginSubpass()
							.AddColorRef(color)
							.SetDepthRef(depth)
							.EndSubpass()
							.AddExternalDependency()
							.Build());

						renderPasses[Fox::Graphics::Managers::Vulkan::RenderPass::OFFSCREEN] = std::move(offscreenRenderPass);
					}
					{
						builder.reset();
						builder = std::make_unique<Fox::Graphics::Vulkan::RenderPassBuilder>(device);

						uint32_t color = builder->AddColorAttachment(surfaceFormat.format,
							VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR,
							VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE,
							VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
						uint32_t depth = builder->AddDepthAttachment(depthFormat);

						std::unique_ptr<Fox::Graphics::Vulkan::RenderPass> defaultRenderPass = std::make_unique<Fox::Graphics::Vulkan::RenderPass>(builder
							->BeginSubpass()
							.AddColorRef(color)
							.SetDepthRef(depth)
							.EndSubpass()
							.AddExternalDependency()
							.Build());

						renderPasses[Fox::Graphics::Managers::Vulkan::RenderPass::DEFAULT] = std::move(defaultRenderPass);

						return true;
					}

				}

			}
		}
	}
}