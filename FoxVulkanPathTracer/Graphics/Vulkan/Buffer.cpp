#include "FoxRenderer.h"

namespace Fox
{
	namespace Graphics
	{
		namespace Vulkan
		{
            void Buffer::CreateBuffer(VkPhysicalDevice physicalDevice,
                VkDeviceSize size,
                VkBufferUsageFlags usage,
                VkMemoryPropertyFlags properties)
            {
                // Create buffer
                VkBufferCreateInfo bufferInfo{};
                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufferInfo.size = size;
                bufferInfo.usage = usage;
                bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

                if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to create buffer!");
                }

                // Get memory requirements
                VkMemoryRequirements memRequirements;
                vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

                // Allocate memory
                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = memRequirements.size;
                allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

                VkMemoryAllocateFlagsInfo flagsInfo{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
                    .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
                };

                allocInfo.pNext = &flagsInfo;

                if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to allocate buffer memory!");
                }

                vkBindBufferMemory(device, buffer, memory, 0);
            }

            uint32_t Buffer::FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
                VkPhysicalDeviceMemoryProperties memProperties;
                vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

                for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
                    if ((typeFilter & (1 << i)) &&
                        (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
                    {
                        return i;
                    }
                }

                throw std::runtime_error("Failed to find suitable memory type!");
            }

            void Buffer::Cleanup() {
                if (buffer != VK_NULL_HANDLE)
                     vkDestroyBuffer(device, buffer, nullptr);
                if (memory != VK_NULL_HANDLE)
                    vkFreeMemory(device, memory, nullptr);
                buffer = VK_NULL_HANDLE;
                memory = VK_NULL_HANDLE;
            }
		}
	}
}