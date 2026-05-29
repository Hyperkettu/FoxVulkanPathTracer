#pragma once

namespace Fox{

    namespace Graphics {

        namespace Vulkan {

            class SwapchainTexture : public Texture {
            public:
                SwapchainTexture(VkDevice device,
                                VkImage swapchainImage,
                                VkExtent2D extent,
                                VkFormat format,
                                VkImageAspectFlags aspectFlags)
                {
					// do not store image in image variable so parent destructor does not try to destroy it
                    this->device = device;
                    this->format = format;

                    VkImageViewCreateInfo viewInfo{};
                    viewInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                    viewInfo.image = swapchainImage;
                    viewInfo.viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D;
                    viewInfo.format = format;
                    viewInfo.subresourceRange.aspectMask = aspectFlags;
                    viewInfo.subresourceRange.levelCount = 1;
                    viewInfo.subresourceRange.layerCount = 1;

                    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
                        throw std::runtime_error("Failed to create image view for swapchain texture!");
                    }
                }

                VkSampler GetSampler() const override {
                    return VK_NULL_HANDLE;
                }

            protected:
                virtual void Destroy() override {
                    // Don't destroy image or memory (owned by swapchain)
                    if (view) { 
                        vkDestroyImageView(device, view, nullptr);
                    }
                }
            };

        }
    }
}