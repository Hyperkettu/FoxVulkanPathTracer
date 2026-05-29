#pragma once

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            class ShaderResourceTexture : public Fox::Graphics::Vulkan::Texture {
            public:
                ShaderResourceTexture() = default;

                ShaderResourceTexture(
                    VkDevice device,
					VkPhysicalDevice physicalDevice,
                    VkExtent3D extent,
                    VkFormat format,
                    VkImageUsageFlags usage,
                    VkImageAspectFlags aspectFlags)
                    : Texture(device, physicalDevice, extent, format, usage, aspectFlags)
                {
                    // --- Create sampler ---
                    VkSamplerCreateInfo samplerInfo{};
                    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                    samplerInfo.magFilter = VK_FILTER_LINEAR;
                    samplerInfo.minFilter = VK_FILTER_LINEAR;
                    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    samplerInfo.maxAnisotropy = 1.0f;

                    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
                        throw std::runtime_error("Failed to create Vulkan sampler!");
                    }
                }

                ShaderResourceTexture(ShaderResourceTexture&& other) noexcept
                    : Texture(std::move(other)), sampler(other.sampler)
                {
                    other.sampler = VK_NULL_HANDLE;
                }

                ShaderResourceTexture& operator=(ShaderResourceTexture&& other) noexcept {
                    if (this != &other) {
                        Texture::operator=(std::move(other));
                        sampler = other.sampler;
                        other.sampler = VK_NULL_HANDLE;
                    }
                    return *this;
                }

                ~ShaderResourceTexture() override {
                    if (sampler) vkDestroySampler(device, sampler, nullptr);
                }

                VkSampler GetSampler() const { return sampler; }

                static Fox::Graphics::Vulkan::ShaderResourceTexture* LoadFromFile(
                    VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    VkCommandPool commandPool,
                    VkQueue graphicsQueue,
                    const std::string& filename);

                uint32_t width = 0;
                uint32_t height = 0;

            private:
                VkSampler sampler = VK_NULL_HANDLE;
				
            };
        }
    }
}