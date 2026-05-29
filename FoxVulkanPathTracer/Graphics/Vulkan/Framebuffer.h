#pragma once

namespace Fox {

    namespace Graphics
    {

        namespace Vulkan {

            class Framebuffer {
            public:
                Framebuffer() = default;

                Framebuffer(VkDevice device,
                    VkRenderPass renderPass,
                    const std::vector<VkImageView>& attachments,
                    uint32_t width,
                    uint32_t height,
                    uint32_t layers = 1)
                    : device(device)
                {
                    VkFramebufferCreateInfo framebufferInfo{};
                    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                    framebufferInfo.renderPass = renderPass;
                    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
                    framebufferInfo.pAttachments = attachments.data();
                    framebufferInfo.width = width;
                    framebufferInfo.height = height;
                    framebufferInfo.layers = layers;

                    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
                        throw std::runtime_error("Failed to create Vulkan framebuffer.");
                    }
                }

                Framebuffer(const Framebuffer&) = delete;
                Framebuffer& operator=(const Framebuffer&) = delete;

                Framebuffer(Framebuffer&& other) noexcept
                    : device(other.device), framebuffer(other.framebuffer)
                {
                    other.framebuffer = VK_NULL_HANDLE;
                    other.device = VK_NULL_HANDLE;
                }

                Framebuffer& operator=(Framebuffer&& other) noexcept
                {
                    if (this != &other) {
                        Destroy();
                        device = other.device;
                        framebuffer = other.framebuffer;
                        other.framebuffer = VK_NULL_HANDLE;
                        other.device = VK_NULL_HANDLE;
                    }
                    return *this;
                }

                ~Framebuffer() { Destroy(); }

                void Destroy()
                {
                    if (framebuffer != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
                        vkDestroyFramebuffer(device, framebuffer, nullptr);
                        framebuffer = VK_NULL_HANDLE;
                    }
                }

                VkFramebuffer Get() const { return framebuffer; }
                operator VkFramebuffer() const { return framebuffer; }

            private:
                VkDevice device = VK_NULL_HANDLE;
                VkFramebuffer framebuffer = VK_NULL_HANDLE;
            };
        }
    }
} 