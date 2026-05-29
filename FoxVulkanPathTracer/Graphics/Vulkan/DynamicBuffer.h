#pragma once

#include "Buffer.h"
#include <vector>
#include <type_traits>
#include <cstring>

namespace Fox {

namespace Graphics {

namespace Vulkan {

    template<typename T>
    class DynamicBuffer {
    public:
        DynamicBuffer() = default;

        DynamicBuffer(VkDevice device,
                      VkPhysicalDevice physicalDevice,
                      VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VkMemoryPropertyFlags memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            : device(device), physicalDevice(physicalDevice), usageFlags(usageFlags), memoryProperties(memoryProperties)
        {
        }

        DynamicBuffer(const DynamicBuffer&) = delete;
        DynamicBuffer& operator=(const DynamicBuffer&) = delete;
        DynamicBuffer(DynamicBuffer&&) noexcept = default;
        DynamicBuffer& operator=(DynamicBuffer&&) noexcept = default;

        ~DynamicBuffer() = default;

        // Upload std::vector<T> or any contiguous range
        void Update(const std::vector<T>& data)
        {
            if (data.empty())
                return;

            VkDeviceSize newSize = sizeof(T) * data.size();
            EnsureBufferSize(newSize);

            void* mapped = buffer->Map();
            std::memcpy(mapped, data.data(), newSize);
            buffer->Unmap();

            currentSize = newSize;
        }

        VkDescriptorBufferInfo DescriptorInfo() const
        {
            VkDescriptorBufferInfo info{};
            info.buffer = buffer ? buffer->Get() : VK_NULL_HANDLE;
            info.offset = 0;
            info.range = currentSize;
            return info;
        }

        const Buffer& GetBuffer() const { return *buffer; }
        VkBuffer Get() const { return buffer ? buffer->Get() : VK_NULL_HANDLE; }

        size_t GetElementCount() const { return currentSize / sizeof(T); }

    private:
        void EnsureBufferSize(VkDeviceSize newSize)
        {
            if (buffer && newSize <= buffer->GetSize())
                return; // no need to recreate

            // Recreate with new size
            buffer = std::make_unique<Buffer>(
                device,
                physicalDevice,
                newSize,
                usageFlags,
                memoryProperties
            );
        }

    private:
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkBufferUsageFlags usageFlags{};
        VkMemoryPropertyFlags memoryProperties{};

        std::unique_ptr<Buffer> buffer;
        VkDeviceSize currentSize = 0;
    };

} // namespace Vulkan
} // namespace Graphics
} // namespace Fox
