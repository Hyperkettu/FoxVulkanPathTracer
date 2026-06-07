#pragma once

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            class StorageTexture : public Fox::Graphics::Vulkan::Texture {
            public:
                StorageTexture() = default;

                StorageTexture(
                    VkDevice device,
					VkPhysicalDevice physicalDevice,
                    VkExtent3D extent,
                    VkFormat format,
                    VkImageUsageFlags usage,
                    VkImageAspectFlags aspectFlags)
                    : Texture(device, physicalDevice, extent, format, usage, aspectFlags)
                {
                }

                StorageTexture(StorageTexture&& other) noexcept
                    : Texture(std::move(other))
                {
                  
                }

                StorageTexture& operator=(StorageTexture&& other) noexcept {
                    if (this != &other) {
                        Texture::operator=(std::move(other));
                    }
                    return *this;
                }

				VkSampler GetSampler() const override { return VK_NULL_HANDLE; }    
	
            };
        }
    }
}