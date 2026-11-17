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
                    const std::string& name = "",
                    VkMemoryPropertyFlags memoryProperties =
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                    : device(device)
                {
                    static_assert(std::is_trivially_copyable_v<T>, "Constant buffer type must be trivially copyable.");

                    buffer = std::make_unique<Fox::Graphics::Vulkan::Buffer>(
                        device,
                        physicalDevice,
                        sizeof(T),
                        VkBufferUsageFlagBits::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
                        memoryProperties
                    );

#ifdef _DEBUG
                    if (name.size() > 0) {
                        Fox::Graphics::Vulkan::CommandList::SetName(name, reinterpret_cast<uint64_t>(buffer->Get()), VkObjectType::VK_OBJECT_TYPE_BUFFER, device);
                        Fox::Graphics::Vulkan::CommandList::PrintBufferNameAndAddress(device, buffer->Get(), name);
                    }
#endif
                }

                ShaderStorageBuffer(VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    const std::string& name = "",
                    const std::vector<T>& arr = {},
                    VkMemoryPropertyFlags memoryProperties =
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                    : device(device)
                {
                    static_assert(std::is_trivially_copyable_v<T>, "Constant buffer type must be trivially copyable.");

                    buffer = std::make_unique<Fox::Graphics::Vulkan::Buffer>(
                        device,
                        physicalDevice,
                        sizeof(T) * arr.size(),
                        VkBufferUsageFlagBits::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        memoryProperties
                    );

#ifdef _DEBUG
                    if (name.size() > 0) {
                        Fox::Graphics::Vulkan::CommandList::SetName(name, reinterpret_cast<uint64_t>(buffer->Get()), VkObjectType::VK_OBJECT_TYPE_BUFFER, device);
                        Fox::Graphics::Vulkan::CommandList::PrintBufferNameAndAddress(device, buffer->Get(), name);
                    }
#endif
                }

                // A buffer in video memory, filled once through a staging copy. For data the GPU reads a lot
                // (scene geometry, materials): the default host-visible memory is system RAM, and every
                // shader read of it crosses PCIe. Not mappable, so Update() cannot be used on it.
                static std::unique_ptr<ShaderStorageBuffer> CreateDeviceLocal(VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    VkCommandPool commandPool,
                    VkQueue queue,
                    const std::string& name,
                    const std::vector<T>& arr)
                {
                    auto result = std::make_unique<ShaderStorageBuffer>(device, physicalDevice, name, arr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

                    const VkDeviceSize size = sizeof(T) * arr.size();
                    Fox::Graphics::Vulkan::Buffer staging(device, physicalDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                    std::memcpy(staging.Map(), arr.data(), size);
                    staging.Unmap();

                    VkBufferCopy region{};
                    region.size = size;
                    Fox::Graphics::Vulkan::CommandList commandList(device, commandPool);
                    commandList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
                        .CopyBuffer(staging.Get(), result->buffer->Get(), { region })
                        .End()
                        .SubmitAndWait(queue);

                    return result;
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

                void Update(const std::vector<T>& arr) {
                    void* data = buffer->Map();
                    std::memcpy(data, arr.data(), sizeof(T) * arr.size());
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

                std::unique_ptr<Fox::Graphics::Vulkan::Buffer>& GetBufferUnique() { return buffer; }


            private:
                VkDevice device;
                std::unique_ptr<Fox::Graphics::Vulkan::Buffer> buffer;
            };
        
        }
    }
}

