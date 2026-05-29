#pragma once

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            class Texture {
            public:
                Texture() = default;

                Texture(VkDevice device,
					VkPhysicalDevice physicalDevice,
                    VkExtent3D extent,
                    VkFormat format,
                    VkImageUsageFlags usage,
                    VkImageAspectFlags aspectFlags,
                    VkMemoryPropertyFlags memoryProperties = VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                    : device(device), extent(extent), format(format)
                {
                    // --- Create Image ---
                    VkImageCreateInfo imageInfo{};
                    imageInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                    imageInfo.imageType = VkImageType::VK_IMAGE_TYPE_2D;
                    imageInfo.extent = extent;
                    imageInfo.mipLevels = 1;
                    imageInfo.arrayLayers = 1;
                    imageInfo.format = format;
                    imageInfo.tiling = VkImageTiling::VK_IMAGE_TILING_OPTIMAL;
                    imageInfo.initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
                    imageInfo.usage = usage;
                    imageInfo.samples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
                    imageInfo.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;

                    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
                        throw std::runtime_error("Failed to create Vulkan image!");
                    }

                    // --- Allocate memory ---
                    VkMemoryRequirements memReq{};
                    vkGetImageMemoryRequirements(device, image, &memReq);

                    VkMemoryAllocateInfo allocInfo{};
                    allocInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                    allocInfo.allocationSize = memReq.size;
					allocInfo.memoryTypeIndex = Buffer::FindMemoryType(physicalDevice, memReq.memoryTypeBits, memoryProperties);

                    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
                        throw std::runtime_error("Failed to allocate Vulkan image memory!");
                    }

                    vkBindImageMemory(device, image, memory, 0);

                    // --- Create image view ---
                    VkImageViewCreateInfo viewInfo{};
                    viewInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                    viewInfo.image = image;
                    viewInfo.viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D;
                    viewInfo.format = format;
                    viewInfo.subresourceRange.aspectMask = aspectFlags;
                    viewInfo.subresourceRange.levelCount = 1;
                    viewInfo.subresourceRange.layerCount = 1;

                    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
                        throw std::runtime_error("Failed to create Vulkan image view!");
                    }
                }

                Texture(const Texture&) = delete;
                Texture& operator=(const Texture&) = delete;

                Texture(Texture&& other) noexcept
                    : device(other.device),
                    image(other.image),
                    view(other.view),
                    memory(other.memory),
                    extent(other.extent),
                    format(other.format)
                {
                    other.image = VK_NULL_HANDLE;
                    other.view = VK_NULL_HANDLE;
                    other.memory = VK_NULL_HANDLE;
                }

                Texture& operator=(Texture&& other) noexcept {
                    if (this != &other) {
                        Destroy();
                        device = other.device;
                        image = other.image;
                        view = other.view;
                        memory = other.memory;
                        extent = other.extent;
                        format = other.format;

                        other.image = VK_NULL_HANDLE;
                        other.view = VK_NULL_HANDLE;
                        other.memory = VK_NULL_HANDLE;
                    }
                    return *this;
                }

                virtual ~Texture() { Destroy(); }

                VkImage GetImage() const { return image; }
                VkImageView GetView() const { return view; }
                VkFormat GetFormat() const { return format; }
                VkExtent3D GetExtent() const { return extent; }

                virtual VkSampler GetSampler() const = 0;

            protected:
                VkDevice device = VK_NULL_HANDLE;
                VkImage image = VK_NULL_HANDLE;
                VkImageView view = VK_NULL_HANDLE;
                VkDeviceMemory memory = VK_NULL_HANDLE;
                VkExtent3D extent{};
                VkFormat format{};

                virtual void Destroy() {
                    if (view) vkDestroyImageView(device, view, nullptr);
                    if (image) vkDestroyImage(device, image, nullptr);
                    if (memory) vkFreeMemory(device, memory, nullptr);
                }
            };
        }
    }
}