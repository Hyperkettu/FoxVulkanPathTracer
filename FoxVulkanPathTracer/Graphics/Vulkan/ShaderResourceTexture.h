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


                // Uploads tightly packed pixels. bytesPerPixel must match the texel size of the format.
                static std::unique_ptr<Fox::Graphics::Vulkan::ShaderResourceTexture> LoadFromPixelData(
                    VkDevice device, 
                    VkPhysicalDevice physicalDevice, 
                    VkCommandPool commandPool,
                    VkQueue graphicsQueue, 
                    const void* pixels, uint32_t width, uint32_t height,
                    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB, uint32_t bytesPerPixel = 4);

                // Loads an image from disk. Radiance .hdr files keep their full range (RGBA16F);
                // everything else is loaded as 8-bit RGBA, interpreted as sRGB unless srgb is false.
                static std::unique_ptr<Fox::Graphics::Vulkan::ShaderResourceTexture> LoadFromFile(
                    VkDevice device, 
                    VkPhysicalDevice physicalDevice,
                    VkCommandPool commandPool,
                    VkQueue graphicsQueue,
                    const std::string& filename,
                    bool srgb = true);

                uint32_t width = 0;
                uint32_t height = 0;

            private:
                VkSampler sampler = VK_NULL_HANDLE;
				
            };
        }
    }
}