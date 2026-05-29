#pragma once

#include "Buffer.h"
#include <cstring>  // for memcpy

namespace Fox
{
    namespace Graphics {
    
        namespace Vulkan {
        
            template<class T>
            class ShaderStorageBuffer {
            public:
                ShaderStorageBuffer() = default;
                ShaderStorageBuffer(VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    VkMemoryPropertyFlags memoryProperties =
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                    : device(device)
                {
                    static_assert(std::is_trivially_copyable_v<T>, "Constant buffer type must be trivially copyable.");

                    buffer = std::make_unique<Fox::Graphics::Vulkan::Buffer>(
                        device,
                        physicalDevice,
                        sizeof(T),
                        VkBufferUsageFlagBits::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
                        memoryProperties
                    );
                }

                ShaderStorageBuffer(const ShaderStorageBuffer&) = delete;
                ShaderStorageBuffer& operator=(const ShaderStorageBuffer&) = delete;
                ShaderStorageBuffer(ShaderStorageBuffer&&) noexcept = default;
                ShaderStorageBuffer& operator=(ShaderStorageBuffer&&) noexcept = default;

                ~ShaderStorageBuffer() = default;

                void Update(const T& newData) {
                    void* data = buffer->Map();
                    std::memcpy(data, &newData, sizeof(T));
                    buffer->Unmap();

                }

                VkDescriptorBufferInfo DescriptorInfo() const {
                    VkDescriptorBufferInfo info{};
                    info.buffer = buffer->Get();
                    info.offset = 0;
                    info.range = sizeof(T);
                    return info;
                }

                const Buffer& GetBuffer() const { return *buffer; }

                const std::unique_ptr<Fox::Graphics::Vulkan::Buffer>& GetBufferUnique() const { return buffer; }


            private:
                VkDevice device;
                std::unique_ptr<Fox::Graphics::Vulkan::Buffer> buffer;
            };
        
        }
    }
}

