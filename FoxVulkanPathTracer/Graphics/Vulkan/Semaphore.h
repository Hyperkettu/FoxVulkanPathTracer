#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            class Semaphore {
            public:
                Semaphore() = default;

                explicit Semaphore(VkDevice device)
                    : device(device)
                {
                    VkSemaphoreCreateInfo createInfo{};
                    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

                    if (vkCreateSemaphore(device, &createInfo, nullptr, &semaphore) != VK_SUCCESS) {
                        throw std::runtime_error("Failed to create Vulkan semaphore!");
                    }
                }

                // No copy allowed
                Semaphore(const Semaphore&) = delete;
                Semaphore& operator=(const Semaphore&) = delete;

                // Move constructor
                Semaphore(Semaphore&& other) noexcept
                    : device(other.device), semaphore(other.semaphore)
                {
                    other.semaphore = VK_NULL_HANDLE;
                }

                // Move assignment
                Semaphore& operator=(Semaphore&& other) noexcept {
                    if (this != &other) {
                        Destroy();
                        device = other.device;
                        semaphore = other.semaphore;
                        other.semaphore = VK_NULL_HANDLE;
                    }
                    return *this;
                }

                ~Semaphore() {
                    Destroy();
                }

                VkSemaphore Get() const { return semaphore; }
                bool IsValid() const { return semaphore != VK_NULL_HANDLE; }

            private:
                VkDevice device = VK_NULL_HANDLE;
                VkSemaphore semaphore = VK_NULL_HANDLE;

                void Destroy() {
                    if (semaphore != VK_NULL_HANDLE) {
                        vkDestroySemaphore(device, semaphore, nullptr);
                        semaphore = VK_NULL_HANDLE;
                    }
                }
            };

        }
    }
}