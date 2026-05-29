#pragma once

#include "Buffer.h"
#include <cstring>  // for memcpy

namespace Fox
{
    namespace Graphics {
    
        namespace Vulkan {
        
            template<class T>
            class ConstantBuffer {
            public:
                ConstantBuffer() = default;
                ConstantBuffer(VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    VkMemoryPropertyFlags memoryProperties =
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                    : device(device)
                {
                    static_assert(std::is_trivially_copyable_v<T>, "Constant buffer type must be trivially copyable.");

                    buffer = std::make_unique<Buffer>(
                        device,
                        physicalDevice,
                        sizeof(T),
                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                        memoryProperties
                    );
                }

                ConstantBuffer(const ConstantBuffer&) = delete;
                ConstantBuffer& operator=(const ConstantBuffer&) = delete;
                ConstantBuffer(ConstantBuffer&&) noexcept = default;
                ConstantBuffer& operator=(ConstantBuffer&&) noexcept = default;

                ~ConstantBuffer() = default;

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

            private:
                VkDevice device;
                std::unique_ptr<Buffer> buffer;
            };
        
        }
    }
}

