#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			class RenderTargetTexture : public Fox::Graphics::Vulkan::Texture {
			public:
    				RenderTargetTexture(VkDevice device,
                            VkPhysicalDevice physicalDevice,
                        	VkExtent3D extent,
                        	VkFormat format,
                        	VkImageAspectFlags aspectFlags)
        			: Fox::Graphics::Vulkan::Texture(device, physicalDevice, extent, format,
                  			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                  			aspectFlags)
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

                    ~RenderTargetTexture() override {
                        if (sampler) vkDestroySampler(device, sampler, nullptr);
                    }

                    RenderTargetTexture& operator= (RenderTargetTexture&& other) {
                        Fox::Graphics::Vulkan::Texture::operator=(std::move(other));
                        sampler = other.sampler;
                        other.sampler = VK_NULL_HANDLE;
                        return *this;
                    }

                    VkSampler GetSampler() const { return sampler; }

            private:
                VkSampler sampler = VK_NULL_HANDLE;
            };		
		}
	}
}