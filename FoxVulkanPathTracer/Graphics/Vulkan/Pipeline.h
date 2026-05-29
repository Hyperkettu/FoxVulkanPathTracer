#pragma once

namespace Fox {
    
    namespace Graphics {
        
        namespace Vulkan {

            class Pipeline {
            public:
                Pipeline() = default;

                Pipeline(VkDevice device,
                        VkPipelineLayout layout,
                        VkPipeline pipeline)
                    : device(device), layout(layout), pipeline(pipeline)
                {}

                // Non-copyable
                Pipeline(const Pipeline&) = delete;
                Pipeline& operator=(const Pipeline&) = delete;

                // Movable
                Pipeline(Pipeline&& other) noexcept
                    : device(other.device),
                    pipeline(other.pipeline),
                    layout(other.layout)
                {
                    other.pipeline = VK_NULL_HANDLE;
                    other.layout = VK_NULL_HANDLE;
                    other.device = VK_NULL_HANDLE;
                }

                Pipeline& operator=(Pipeline&& other) noexcept
                {
                    if (this != &other) {
                        Destroy();
                        device = other.device;
                        pipeline = other.pipeline;
                        layout = other.layout;

                        other.pipeline = VK_NULL_HANDLE;
                        other.layout = VK_NULL_HANDLE;
                        other.device = VK_NULL_HANDLE;
                    }
                    return *this;
                }

                ~Pipeline() { Destroy(); }

                void Destroy()
                {
                    if (device) {
                        if (pipeline) {
                            vkDestroyPipeline(device, pipeline, nullptr);
                        }

                        pipeline = VK_NULL_HANDLE;
                        layout = VK_NULL_HANDLE;
                    }
                }

                VkPipeline Get() const { return pipeline; }
                VkPipelineLayout GetLayout() const { return layout; }

                operator VkPipeline() const { return pipeline; }

            private:
                VkDevice device = VK_NULL_HANDLE;
                VkPipeline pipeline = VK_NULL_HANDLE;
                VkPipelineLayout layout = VK_NULL_HANDLE;
            };
        }
    }
} // namespace Fox::Graphics::Vulkan