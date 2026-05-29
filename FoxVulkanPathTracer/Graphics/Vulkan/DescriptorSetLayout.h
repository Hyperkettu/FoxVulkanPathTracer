#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <vector>

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            class DescriptorSetLayout {
            public:
                DescriptorSetLayout() = default;

                DescriptorSetLayout(VkDevice device,
                    const std::vector<VkDescriptorSetLayoutBinding>& bindings,
                    VkDescriptorSetLayoutCreateFlags flags = 0)
                    : device(device)
                {
                    VkDescriptorSetLayoutCreateInfo layoutInfo{};
                    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
                    layoutInfo.pBindings = bindings.data();
                    layoutInfo.flags = flags;

                    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
                        throw std::runtime_error("Failed to create Vulkan descriptor set layout!");
                    }
                }

                DescriptorSetLayout(const DescriptorSetLayout&) = delete;
                DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

                DescriptorSetLayout(DescriptorSetLayout&& other) noexcept
                    : device(other.device), layout(other.layout)
                {
                    other.layout = VK_NULL_HANDLE;
                }

                DescriptorSetLayout& operator=(DescriptorSetLayout&& other) noexcept {
                    if (this != &other) {
                        Destroy();
                        device = other.device;
                        layout = other.layout;
                        other.layout = VK_NULL_HANDLE;
                    }
                    return *this;
                }

                ~DescriptorSetLayout() {
                    Destroy();
                }

                VkDescriptorSetLayout Get() const { return layout; }

            private:
                VkDevice device = VK_NULL_HANDLE;
                VkDescriptorSetLayout layout = VK_NULL_HANDLE;

                void Destroy() {
                    if (layout != VK_NULL_HANDLE) {
                        vkDestroyDescriptorSetLayout(device, layout, nullptr);
                        layout = VK_NULL_HANDLE;
                    }
                }
            };
        }
    }
} 