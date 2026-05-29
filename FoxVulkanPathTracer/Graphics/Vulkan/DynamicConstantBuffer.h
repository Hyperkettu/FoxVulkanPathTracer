#pragma once

#include "Buffer.h"
#include <vector>
#include <cstring>  // for memcpy

namespace Fox {
namespace Graphics {
namespace Vulkan {

    template<typename T>
    class DynamicConstantBuffer {
    public:
        DynamicConstantBuffer() = default;

        DynamicConstantBuffer(VkDevice device,
                              VkPhysicalDevice physicalDevice,
                              VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              VkMemoryPropertyFlags memoryProperties =
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            : device(device), physicalDevice(physicalDevice), usageFlags(usageFlags), memoryProperties(memoryProperties)
        {
        }

        DynamicConstantBuffer(const DynamicConstantBuffer&) = delete;
        DynamicConstantBuffer& operator=(const DynamicConstantBuffer&) = delete;
        DynamicConstantBuffer(DynamicConstantBuffer&&) noexcept = default;
        DynamicConstantBuffer& operator=(DynamicConstantBuffer&&) noexcept = default;

        ~DynamicConstantBuffer() = default;

        // Upload vector data to GPU
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
                return; // buffer is large enough, reuse

            // Recreate buffer with new size
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