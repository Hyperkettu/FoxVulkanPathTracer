#include "FoxRenderer.h"

#include <glm/gtc/packing.hpp>

// stb_image is declared through FoxRenderer.h; its implementation is compiled in GLTFLoader.cpp.

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            std::unique_ptr<ShaderResourceTexture> ShaderResourceTexture::LoadFromFile(
                VkDevice device,
                VkPhysicalDevice physicalDevice,
                VkCommandPool commandPool,
                VkQueue graphicsQueue,
                const std::string& filename,
                bool srgb)
            {
                int width = 0, height = 0, channels = 0;

                if (stbi_is_hdr(filename.c_str())) {
                    float* pixels = stbi_loadf(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);
                    if (!pixels)
                        throw std::runtime_error("Failed to load HDR image: " + filename);

                    // Half floats keep the dynamic range at half the memory, and unlike RGBA32F
                    // the format is guaranteed to support linear filtering.
                    const size_t componentCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
                    std::vector<uint16_t> halfPixels(componentCount);
                    for (size_t i = 0; i < componentCount; i++) {
                        halfPixels[i] = glm::packHalf1x16(std::min(pixels[i], 65504.0f));
                    }
                    stbi_image_free(pixels);

                    return LoadFromPixelData(device, physicalDevice, commandPool, graphicsQueue,
                        halfPixels.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                        VK_FORMAT_R16G16B16A16_SFLOAT, 4 * sizeof(uint16_t));
                }

                stbi_uc* pixels = stbi_load(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);
                if (!pixels)
                    throw std::runtime_error("Failed to load texture image: " + filename);

                auto texture = LoadFromPixelData(device, physicalDevice, commandPool, graphicsQueue,
                    pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                    srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM, 4);

                stbi_image_free(pixels);

                return texture;
            }

            std::unique_ptr<ShaderResourceTexture> ShaderResourceTexture::LoadFromPixelData(
                VkDevice device,
                VkPhysicalDevice physicalDevice,
                VkCommandPool commandPool,
                VkQueue graphicsQueue,
                const void* pixels, uint32_t width, uint32_t height,
                VkFormat format, uint32_t bytesPerPixel)
            {
                auto texture = std::make_unique<ShaderResourceTexture>(
                    device,
                    physicalDevice,
                    VkExtent3D{ width, height, 1 },
                    format,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT);

                texture->width = width;
                texture->height = height;

                const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * bytesPerPixel;

                Buffer stagingBuffer(device, physicalDevice, imageSize,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

                void* stagingBufferData = stagingBuffer.Map();
                memcpy(stagingBufferData, pixels, static_cast<size_t>(imageSize));
                stagingBuffer.Unmap();

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
                region.imageExtent = { width, height, 1 };

                // The textures are sampled from ray tracing shaders, not fragment shaders.
                const VkPipelineStageFlags samplingStages =
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;

                CommandList cmdList(device, commandPool);
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
                        samplingStages)
                    .End()
                    .SubmitAndWait(graphicsQueue);

                return texture;
            }

        } // namespace Vulkan
    }     // namespace Graphics
} // namespace Fox
