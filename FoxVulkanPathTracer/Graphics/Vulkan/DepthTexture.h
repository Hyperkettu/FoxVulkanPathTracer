#pragma once

namespace Fox { 

    namespace Graphics {

        namespace Vulkan {

            class DepthTexture : public Texture {
            public:
                DepthTexture() = default;

                DepthTexture(VkDevice device,
					         VkPhysicalDevice physicalDevice,
                             VkExtent3D extent,
                             VkFormat format = VK_FORMAT_D32_SFLOAT,
                             bool sampled = false)
                    : Texture(device,
						      physicalDevice,
                              extent,
                              format,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                  (sampled ? VK_IMAGE_USAGE_SAMPLED_BIT : 0),
                              DepthTexture::getAspectMask(format))
                {
                }

                VkSampler GetSampler() const override {
                    return VK_NULL_HANDLE;
                }

                static VkImageAspectFlags getAspectMask(VkFormat format) {
                    if (format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                        format == VK_FORMAT_D24_UNORM_S8_UINT)
                        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                    return VK_IMAGE_ASPECT_DEPTH_BIT;
                }
            };
            }
        }
} // namespace Fox::Graphics::Vulkan