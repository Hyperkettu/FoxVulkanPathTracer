#include "FoxRenderer.h"

namespace Fox {
	namespace Graphics {
		namespace Vulkan {

			Swapchain::Swapchain(
				VkDevice device,
				VkRenderPass renderPass,
				VkImageView depthImageView,
				VkSurfaceKHR surface,
				VkSurfaceFormatKHR surfaceFormat,
				VkSurfaceCapabilitiesKHR capabilities,
				uint32_t numImages) {

				this->device = device;

				VkSwapchainCreateInfoKHR swapchainInfo{};
				swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
				swapchainInfo.surface = surface;
				swapchainInfo.minImageCount = capabilities.minImageCount + 1;
				if (capabilities.maxImageCount > 0) { // 0 means "no upper limit"
					swapchainInfo.minImageCount = std::min(swapchainInfo.minImageCount, capabilities.maxImageCount);
				}
				swapchainInfo.imageFormat = surfaceFormat.format;
				swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
				swapchainInfo.imageExtent = capabilities.currentExtent;
				swapchainInfo.imageArrayLayers = 1;
				swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
				swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
				swapchainInfo.preTransform = capabilities.currentTransform;
				swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
				swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
				swapchainInfo.clipped = VK_TRUE;

				if (vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain) != VK_SUCCESS) {
					throw std::runtime_error("Failed to create the swapchain!");
				}

				RetrieveSwapchainImages(capabilities.currentExtent, surfaceFormat.format);
				CreateSwapchainFramebuffers(depthImageView, renderPass, capabilities.currentExtent.width, capabilities.currentExtent.height);
			}

			void Swapchain::Destroy() {
				for (size_t i = 0; i < swapchainFramebuffers.size(); i++) {
					swapchainFramebuffers[i] = nullptr;
				}

				for (size_t i = 0; i < swapchainTextures.size(); i++) {
					swapchainTextures[i] = nullptr;
				}

				vkDestroySwapchainKHR(device, swapchain, nullptr);
			}

			void Swapchain::RetrieveSwapchainImages(VkExtent2D extent, VkFormat format) {
				uint32_t imageCount;
				vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
				swapchainImages = std::vector<VkImage>(imageCount);
				vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

				for (uint32_t i = 0; i < imageCount; i++)
				{
					swapchainTextures.push_back(std::make_unique<SwapchainTexture>(
						device,
						swapchainImages[i],
						extent,
						format,
						VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT
					));
				}
			}

			void Swapchain::CreateSwapchainFramebuffers(VkImageView depthImageView, VkRenderPass renderPass, uint32_t width, uint32_t height) {
				swapchainFramebuffers.resize(swapchainTextures.size());

				for (size_t i = 0; i < swapchainFramebuffers.size(); i++) {

					std::array<VkImageView, 2> attachments = {
						swapchainTextures[i]->GetView(),
						depthImageView
					};

					swapchainFramebuffers[i] = std::make_unique<Fox::Graphics::Vulkan::Framebuffer>(
						device,
						renderPass,
						std::vector<VkImageView>(attachments.begin(), attachments.end()),
						width,
						height
					);	
				}
			}
		}
	}
}