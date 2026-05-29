#pragma once

namespace Fox {

    namespace Graphics {

		namespace Vulkan {

            class Buffer {
            public:

             //   Buffer() = default;

                Buffer(VkDevice device,
                       VkPhysicalDevice physicalDevice,
                       VkDeviceSize size,
                       VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties)
                    : device(device), buffer(VK_NULL_HANDLE), memory(VK_NULL_HANDLE), size(size)
                {
                    CreateBuffer(physicalDevice, size, usage, properties);
                }

                // Move constructor
                Buffer(Buffer&& other) noexcept
                    : device(other.device),
                      buffer(other.buffer),
                      memory(other.memory),
                      size(other.size)
                {
                    other.buffer = VK_NULL_HANDLE;
                    other.memory = VK_NULL_HANDLE;
                }

                // No copying
                Buffer(const Buffer&) = delete;
                Buffer& operator=(const Buffer&) = delete;

                // Move assignment
                Buffer& operator=(Buffer&& other) noexcept {
                    if (this != &other) {
                        Cleanup();
                        device = other.device;
                        buffer = other.buffer;
                        memory = other.memory;
                        size = other.size;
                        other.buffer = VK_NULL_HANDLE;
                        other.memory = VK_NULL_HANDLE;
                    }
                    return *this;
                }

                ~Buffer() {
                    Cleanup();
                }

                VkBuffer Get() const { return buffer; }
                VkDeviceMemory GetMemory() const { return memory; }
                VkDeviceSize GetSize() const { return size; }

                void* Map(VkDeviceSize offset = 0, VkDeviceSize mapSize = VK_WHOLE_SIZE) {
                    void* data;
                    vkMapMemory(device, memory, offset, mapSize, 0, &data);
                    return data;
                }

                void Unmap() {
                    vkUnmapMemory(device, memory);
                }

                static uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

            private:
                VkDevice device;
                VkBuffer buffer;
                VkDeviceMemory memory;
                VkDeviceSize size;

                void CreateBuffer(VkPhysicalDevice physicalDevice,
                    VkDeviceSize size,
                    VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties);
                
                void Cleanup();
            };

        } 
    }
} 