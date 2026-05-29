#pragma once

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

                class CommandPool {
                public:
                    CommandPool() = default;

                    CommandPool(VkDevice device, uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
                        : device(device)
                    {
                        VkCommandPoolCreateInfo poolInfo{};
                        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                        poolInfo.queueFamilyIndex = queueFamilyIndex;
                        poolInfo.flags = flags;

                        if (vkCreateCommandPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
                            throw std::runtime_error("Failed to create command pool!");
                    }

                    CommandPool(const CommandPool&) = delete;
                    CommandPool& operator=(const CommandPool&) = delete;

                    CommandPool(CommandPool&& other) noexcept
                        : device(other.device), pool(other.pool)
                    {
                        other.pool = VK_NULL_HANDLE;
                        other.device = VK_NULL_HANDLE;
                    }

                    CommandPool& operator=(CommandPool&& other) noexcept
                    {
                        if (this != &other) {
                            Destroy();
                            device = other.device;
                            pool = other.pool;
                            other.pool = VK_NULL_HANDLE;
                            other.device = VK_NULL_HANDLE;
                        }
                        return *this;
                    }

                    ~CommandPool() { Destroy(); }

                    void Destroy()
                    {
                        if (pool && device) {
                            vkDestroyCommandPool(device, pool, nullptr);
                            pool = VK_NULL_HANDLE;
                        }
                    }

                    void Reset(VkCommandPoolResetFlags flags = 0) const
                    {
                        vkResetCommandPool(device, pool, flags);
                    }

                    VkCommandPool Get() const { return pool; }
                    operator VkCommandPool() const { return pool; }

                private:
                    VkDevice device = VK_NULL_HANDLE;
                    VkCommandPool pool = VK_NULL_HANDLE;
            };
        } 
    }
}