#include "FoxRenderer.h"

#define STB_IMAGE_IMPLEMENTATION
#include "STB_image/stb_image.h"

namespace Fox {

    namespace Graphics {

		namespace Vulkan {

            Fox::Graphics::Vulkan::ShaderResourceTexture* ShaderResourceTexture::LoadFromFile(
                VkDevice device,
                VkPhysicalDevice physicalDevice,
                VkCommandPool commandPool,
                VkQueue graphicsQueue,
                const std::string& filename)
            {
                int texWidth, texHeight, texChannels;
                stbi_uc* pixels = stbi_load(filename.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
                if (!pixels)
                    throw std::runtime_error("Failed to load texture image: " + filename);

               ShaderResourceTexture* texture = new Fox::Graphics::Vulkan::ShaderResourceTexture(
                    ShaderResourceTexture(
                        device,
                        physicalDevice,
                        VkExtent3D{ static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 },
                        VK_FORMAT_R8G8B8A8_SRGB,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT
                    ));

                texture->width = static_cast<uint32_t>(texWidth);
                texture->height = static_cast<uint32_t>(texHeight);
                VkDeviceSize imageSize = texture->width * texture->height * 4;

                Fox::Graphics::Vulkan::Buffer stagingBuffer(device, physicalDevice, imageSize,
                    VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                    VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

                void* stagingBufferData = stagingBuffer.Map();
                memcpy(stagingBufferData, pixels, static_cast<size_t>(imageSize));
                stagingBuffer.Unmap();

                stbi_image_free(pixels);

                // Transition to TRANSFER_DST_OPTIMAL
                VkImageSubresourceRange subresource{};
                subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                subresource.baseMipLevel = 0;
                subresource.levelCount = 1;
                subresource.baseArrayLayer = 0;
                subresource.layerCount = 1;

                VkBufferImageCopy region{};
                region.bufferOffset = 0;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = 0;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = { 0, 0, 0 };
                region.imageExtent = { texture->width, texture->height, 1 };

                Fox::Graphics::Vulkan::CommandList cmdList(device, commandPool);
                cmdList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
                    .TransitionImageLayout(texture->image,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        subresource)
                    .CopyBufferToImage(
                        stagingBuffer.Get(),
                        texture->image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        region)
                    .TransitionImageLayout(texture->image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        subresource,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
                    .End()
                    .SubmitAndWait(graphicsQueue);

                return texture;
            }
   
         } // namespace Vulkan
    }     // namespace Graphics
} // namespace Fox