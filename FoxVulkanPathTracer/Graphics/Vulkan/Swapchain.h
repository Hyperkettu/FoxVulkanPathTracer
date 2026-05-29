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
					uint32_t numImages);

				inline uint32_t AcquireNextImage(VkSemaphore imageAvailableSemaphore) {
					uint32_t imageIndex;
					vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
						imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

					return imageIndex;
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
				VkDevice device;
				VkSwapchainKHR swapchain;

				std::vector<std::unique_ptr<Fox::Graphics::Vulkan::SwapchainTexture>> swapchainTextures;
				std::vector<std::unique_ptr<Fox::Graphics::Vulkan::Framebuffer>> swapchainFramebuffers;
				std::vector<VkImage> swapchainImages;
			};
		}
	}
}