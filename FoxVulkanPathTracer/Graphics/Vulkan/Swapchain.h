#pragma once 

namespace Fox {
	namespace Graphics {
		namespace Vulkan {
			class Swapchain {
			public:
				Swapchain() = default;

				Swapchain(
					VkDevice device,
					VkRenderPass renderPass,
					VkImageView depthImageView,
					VkSurfaceKHR surface,
					VkSurfaceFormatKHR surfaceFormat,
					VkSurfaceCapabilitiesKHR capabilities,
					VkPresentModeKHR presentMode);

				// Returns VK_ERROR_OUT_OF_DATE_KHR when the swapchain no longer matches the surface and must be recreated.
				inline VkResult AcquireNextImage(VkSemaphore imageAvailableSemaphore, uint32_t& imageIndex) {
					return vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
						imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
				}

				inline uint32_t GetImageCount() const {
					return static_cast<uint32_t>(swapchainImages.size());
				}

				inline VkImage GetImage(uint32_t imageIndex) {
					return swapchainImages[imageIndex];
				}

				inline VkFramebuffer GetFramebuffer(uint32_t index) const {
					return swapchainFramebuffers[index]->Get();
				}

				inline VkSwapchainKHR Get() const {
					return swapchain;
				}

				virtual ~Swapchain() {
					Destroy();
				}
			protected:
				virtual void Destroy();
				void RetrieveSwapchainImages(VkExtent2D extent, VkFormat format);
				void CreateSwapchainFramebuffers(VkImageView depthImageView, VkRenderPass renderPass, uint32_t width, uint32_t height);

			private:
				VkDevice device = VK_NULL_HANDLE;
				VkSwapchainKHR swapchain = VK_NULL_HANDLE;

				std::vector<std::unique_ptr<Fox::Graphics::Vulkan::SwapchainTexture>> swapchainTextures;
				std::vector<std::unique_ptr<Fox::Graphics::Vulkan::Framebuffer>> swapchainFramebuffers;
				std::vector<VkImage> swapchainImages;
			};
		}
	}
}