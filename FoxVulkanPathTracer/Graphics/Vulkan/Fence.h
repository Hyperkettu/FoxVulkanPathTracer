#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>

namespace Fox {
    
    namespace Graphics {
    
        namespace Vulkan {

            class Fence {
            public:
                Fence() = default;

                Fence(VkDevice device, bool signaled = false)
                    : device(device)
                {
                    VkFenceCreateInfo fenceInfo{};
                    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                    fenceInfo.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

                    if (vkCreateFence(device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
                        throw std::runtime_error("Failed to create Vulkan fence!");
                    }
                }

                // No copying
                Fence(const Fence&) = delete;
                Fence& operator=(const Fence&) = delete;

                // Move semantics
                Fence(Fence&& other) noexcept
                    : device(other.device), fence(other.fence)
                {
                    other.fence = VK_NULL_HANDLE;
                }

                Fence& operator=(Fence&& other) noexcept {
                    if (this != &other) {
                        destroy();
                        device = other.device;
                        fence = other.fence;
                        other.fence = VK_NULL_HANDLE;
                    }
                    return *this;
                }

                ~Fence() {
                    destroy();
                }

                // Wait for the fence to signal
                void Wait(uint64_t timeout = UINT64_MAX) const {
                    if (fence != VK_NULL_HANDLE) {
                        vkWaitForFences(device, 1, &fence, VK_TRUE, timeout);
                    }
                }

                // Reset the fence to unsignaled state
                void Reset() const {
                    if (fence != VK_NULL_HANDLE) {
                        vkResetFences(device, 1, &fence);
                    }
                }

                // Return the raw VkFence
                VkFence Get() const { return fence; }

                // Check if fence is valid
                bool IsValid() const { return fence != VK_NULL_HANDLE; }

            private:
                VkDevice device = VK_NULL_HANDLE;
                VkFence fence = VK_NULL_HANDLE;

                void destroy() {
                    if (fence != VK_NULL_HANDLE) {
                        vkDestroyFence(device, fence, nullptr);
                        fence = VK_NULL_HANDLE;
                    }
                }
            };

    } 
    }
}