#pragma once

namespace Fox {

    namespace Graphics {
        namespace Vulkan {

            class RenderPassBuilder {
            public:
                explicit RenderPassBuilder(VkDevice device)
                    : device(device)
                {
                }

                // --- Add color attachment ---
                uint32_t AddColorAttachment(
                    VkFormat format,
                    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                {
                    VkAttachmentDescription desc{};
                    desc.format = format;
                    desc.samples = VK_SAMPLE_COUNT_1_BIT;
                    desc.loadOp = loadOp;
                    desc.storeOp = storeOp;
                    desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    desc.finalLayout = finalLayout;

                    attachments.push_back(desc);
                    return static_cast<uint32_t>(attachments.size() - 1);
                }

                // --- Add depth attachment ---
                uint32_t AddDepthAttachment(
                    VkFormat format,
                    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE)
                {
                    VkAttachmentDescription desc{};
                    desc.format = format;
                    desc.samples = VK_SAMPLE_COUNT_1_BIT;
                    desc.loadOp = loadOp;
                    desc.storeOp = storeOp;
                    desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    desc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                    attachments.push_back(desc);
                    return static_cast<uint32_t>(attachments.size() - 1);
                }

                // --- Add resolve attachment (for MSAA) ---
                uint32_t AddResolveAttachment(
                    VkFormat format,
                    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                {
                    VkAttachmentDescription desc{};
                    desc.format = format;
                    desc.samples = VK_SAMPLE_COUNT_1_BIT; // resolves are single-sample
                    desc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    desc.finalLayout = finalLayout;

                    attachments.push_back(desc);
                    return static_cast<uint32_t>(attachments.size() - 1);
                }

                
                RenderPassBuilder& BeginSubpass(VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS)
                {
                    currentSubpass = {};
                    currentSubpass.pipelineBindPoint = bindPoint;
                    subpassColorRefs.clear();
                    subpassInputRefs.clear();
                    subpassResolveRefs.clear();
                    hasDepthAttachment = false;
                    return *this;
                }

                // --- Attach color attachment to current subpass ---
                RenderPassBuilder& AddColorRef(uint32_t attachmentIndex)
                {
                    VkAttachmentReference ref{};
                    ref.attachment = attachmentIndex;
                    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    subpassColorRefs.push_back(ref);
                    return *this;
                }

                // --- Attach depth attachment to current subpass ---
                RenderPassBuilder& SetDepthRef(uint32_t attachmentIndex)
                {
                    depthRef.attachment = attachmentIndex;
                    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    hasDepthAttachment = true;
                    return *this;
                }

                // --- Attach input attachment (for subpass dependency) ---
                RenderPassBuilder& AddInputRef(uint32_t attachmentIndex)
                {
                    VkAttachmentReference ref{};
                    ref.attachment = attachmentIndex;
                    ref.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    subpassInputRefs.push_back(ref);
                    return *this;
                }

                // --- Attach resolve attachment (for MSAA resolve) ---
                RenderPassBuilder& AddResolveRef(uint32_t attachmentIndex)
                {
                    VkAttachmentReference ref{};
                    ref.attachment = attachmentIndex;
                    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    subpassResolveRefs.push_back(ref);
                    return *this;
                }

                // --- End and store current subpass ---
                RenderPassBuilder& EndSubpass()
                {
                    currentSubpass.colorAttachmentCount = static_cast<uint32_t>(subpassColorRefs.size());
                    currentSubpass.pColorAttachments = subpassColorRefs.empty() ? nullptr : subpassColorRefs.data();
                    currentSubpass.pInputAttachments = subpassInputRefs.empty() ? nullptr : subpassInputRefs.data();
                    currentSubpass.inputAttachmentCount = static_cast<uint32_t>(subpassInputRefs.size());
                    currentSubpass.pResolveAttachments = subpassResolveRefs.empty() ? nullptr : subpassResolveRefs.data();
                    currentSubpass.pDepthStencilAttachment = hasDepthAttachment ? &depthRef : nullptr;

                    subpasses.push_back(currentSubpass);
                    return *this;
                }

                // --- Add subpass dependency ---
                RenderPassBuilder& AddDependency(
                    uint32_t srcSubpass,
                    uint32_t dstSubpass,
                    VkPipelineStageFlags srcStageMask,
                    VkPipelineStageFlags dstStageMask,
                    VkAccessFlags srcAccessMask,
                    VkAccessFlags dstAccessMask)
                {
                    VkSubpassDependency dep{};
                    dep.srcSubpass = srcSubpass;
                    dep.dstSubpass = dstSubpass;
                    dep.srcStageMask = srcStageMask;
                    dep.dstStageMask = dstStageMask;
                    dep.srcAccessMask = srcAccessMask;
                    dep.dstAccessMask = dstAccessMask;
                    dependencies.push_back(dep);
                    return *this;
                }

                // --- Common shortcut: add external → subpass dependency ---
                RenderPassBuilder& AddExternalDependency(
                    VkPipelineStageFlags stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VkAccessFlags accessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
                {
                    return AddDependency(
                        VK_SUBPASS_EXTERNAL,
                        0,
                        stageMask,
                        stageMask,
                        0,
                        accessMask
                    );
                }

                // --- Final build ---
                Fox::Graphics::Vulkan::RenderPass Build() const
                {
                    if (subpasses.empty()) {
                        throw std::runtime_error("RenderPassBuilder: at least one subpass required.");
                    }
                    return Fox::Graphics::Vulkan::RenderPass(device, attachments, subpasses, dependencies);
                }

            private:
                VkDevice device = VK_NULL_HANDLE;
                std::vector<VkAttachmentDescription> attachments;

                std::vector<VkSubpassDescription> subpasses;
                std::vector<VkSubpassDependency> dependencies;

                // temp subpass data
                VkSubpassDescription currentSubpass{};
                std::vector<VkAttachmentReference> subpassColorRefs;
                std::vector<VkAttachmentReference> subpassInputRefs;
                std::vector<VkAttachmentReference> subpassResolveRefs;

                VkAttachmentReference depthRef{};
                bool hasDepthAttachment = false;
            };
        }
    }
} // namespace Fox::Graphics::Vulkan