#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <vector>

namespace Fox::Graphics::Vulkan {

class RenderPass {
public:
    RenderPass() = default;

    RenderPass(VkDevice device,
               const std::vector<VkAttachmentDescription>& attachments,
               const std::vector<VkSubpassDescription>& subpasses,
               const std::vector<VkSubpassDependency>& dependencies = {})
        : device(device)
    {
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
        renderPassInfo.pSubpasses = subpasses.data();
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan render pass!");
        }
    }

    // Move-only
    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

    RenderPass(RenderPass&& other) noexcept
        : device(other.device), renderPass(other.renderPass)
    {
        other.renderPass = VK_NULL_HANDLE;
    }

    RenderPass& operator=(RenderPass&& other) noexcept {
        if (this != &other) {
            Destroy();
            device = other.device;
            renderPass = other.renderPass;
            other.renderPass = VK_NULL_HANDLE;
        }
        return *this;
    }

    ~RenderPass() {
        Destroy();
    }

    VkRenderPass Get() const { return renderPass; }
    bool IsValid() const { return renderPass != VK_NULL_HANDLE; }

private:
    VkDevice device = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;

    void Destroy() {
        if (renderPass) {
            vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }
    }
};

} // namespace Fox::Graphics::Vulkan