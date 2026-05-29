#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <vector>

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            class DescriptorPool {
            public:
                DescriptorPool() = default;

                DescriptorPool(VkDevice device,
                               const std::vector<VkDescriptorPoolSize>& poolSizes,
                               uint32_t maxSets,
                               VkDescriptorPoolCreateFlags flags = 0)
                    : device(device)
                {
                    VkDescriptorPoolCreateInfo poolInfo{};
                    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
                    poolInfo.pPoolSizes = poolSizes.data();
                    poolInfo.maxSets = maxSets;
                    poolInfo.flags = flags;

                    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
                        throw std::runtime_error("Failed to create Vulkan descriptor pool!");
                    }
                }

                DescriptorPool(const DescriptorPool&) = delete;
                DescriptorPool& operator=(const DescriptorPool&) = delete;

                DescriptorPool(DescriptorPool&& other) noexcept
                    : device(other.device), pool(other.pool)
                {
                    other.pool = VK_NULL_HANDLE;
                }

                DescriptorPool& operator=(DescriptorPool&& other) noexcept {
                    if (this != &other) {
                        Destroy();
                        device = other.device;
                        pool = other.pool;
                        other.pool = VK_NULL_HANDLE;
                    }
                    return *this;
                }

                ~DescriptorPool() {
                    Destroy();
                }

                VkDescriptorPool Get() const { return pool; }

                /// Reset all allocated descriptor sets
                void Reset() const {
                    vkResetDescriptorPool(device, pool, 0);
                }

            private:
                VkDevice device = VK_NULL_HANDLE;
                VkDescriptorPool pool = VK_NULL_HANDLE;

                void Destroy() {
                    if (pool != VK_NULL_HANDLE) {
                        vkDestroyDescriptorPool(device, pool, nullptr);
                        pool = VK_NULL_HANDLE;
                    }
                }
            };

} 
}
}