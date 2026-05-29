#pragma once

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

                class PipelineLayout {
                public:
                    PipelineLayout() = default;

                    PipelineLayout(VkDevice device,
                                const std::vector<VkDescriptorSetLayout>& setLayouts = {},
                                const std::vector<VkPushConstantRange>& pushConstants = {})
                        : device(device)
                    {
                        VkPipelineLayoutCreateInfo layoutInfo{};
                        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
                        layoutInfo.pSetLayouts = setLayouts.data();
                        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
                        layoutInfo.pPushConstantRanges = pushConstants.data();

                        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
                            throw std::runtime_error("Failed to create pipeline layout.");
                        }
                    }

                    // Non-copyable
                    PipelineLayout(const PipelineLayout&) = delete;
                    PipelineLayout& operator=(const PipelineLayout&) = delete;

                    // Movable
                    PipelineLayout(PipelineLayout&& other) noexcept
                        : device(other.device), layout(other.layout)
                    {
                        other.layout = VK_NULL_HANDLE;
                        other.device = VK_NULL_HANDLE;
                    }

                    PipelineLayout& operator=(PipelineLayout&& other) noexcept
                    {
                        if (this != &other) {
                            Destroy();
                            device = other.device;
                            layout = other.layout;
                            other.layout = VK_NULL_HANDLE;
                            other.device = VK_NULL_HANDLE;
                        }
                        return *this;
                    }

                    ~PipelineLayout() { Destroy(); }

                    void Destroy()
                    {
                        if (layout != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
                            vkDestroyPipelineLayout(device, layout, nullptr);
                            layout = VK_NULL_HANDLE;
                        }
                    }

                    VkPipelineLayout Get() const { return layout; }
                    operator VkPipelineLayout() const { return layout; }

                private:
                    VkDevice device = VK_NULL_HANDLE;
                    VkPipelineLayout layout = VK_NULL_HANDLE;
                };

         }
    }
}