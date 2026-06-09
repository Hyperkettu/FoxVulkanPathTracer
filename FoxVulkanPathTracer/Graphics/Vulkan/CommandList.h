#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <vector>

namespace Fox {

    namespace Scene {
        
        namespace RayTracing {
         
            class RayTracingScene;
        }
    }
    
	namespace Graphics {

		namespace Vulkan {

                class CommandList {
                public:
                    CommandList() = default;

                    CommandList(VkDevice device, VkCommandPool pool)
                        : device(device), pool(pool)
                    {
                        VkCommandBufferAllocateInfo allocInfo{};
                        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                        allocInfo.commandPool = pool;
                        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                        allocInfo.commandBufferCount = 1;

                        if (vkAllocateCommandBuffers(device, &allocInfo, &cmd) != VK_SUCCESS)
                            throw std::runtime_error("Failed to allocate command buffer!");
                    }

                    CommandList(const CommandList&) = delete;
                    CommandList& operator=(const CommandList&) = delete;

                    // Movable
                    CommandList(CommandList&& other) noexcept
                        : device(other.device), pool(other.pool), cmd(other.cmd)
                    {
                        other.cmd = VK_NULL_HANDLE;
                        other.device = VK_NULL_HANDLE;
                        other.pool = VK_NULL_HANDLE;
                    }

                    CommandList& operator=(CommandList&& other) noexcept
                    {
                        if (this != &other) {
                            Free();
                            device = other.device;
                            pool = other.pool;
                            cmd = other.cmd;
                            other.cmd = VK_NULL_HANDLE;
                            other.device = VK_NULL_HANDLE;
                            other.pool = VK_NULL_HANDLE;
                        }
                        return *this;
                    }

                    ~CommandList() { Free(); }

                    void Free()
                    {
                        if (cmd && device)
                            vkFreeCommandBuffers(device, pool, 1, &cmd);
                        cmd = VK_NULL_HANDLE;
                    }

                    // --- Recording API ---
                    CommandList& Begin(VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
                    {
                        VkCommandBufferBeginInfo beginInfo{};
                        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                        beginInfo.flags = flags;

                        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
                            throw std::runtime_error("Failed to begin command buffer!");
                        return *this;
                    }

                    CommandList& BeginRenderPass(VkRenderPass renderPass, VkFramebuffer framebuffer, VkExtent2D extent, const VkClearValue* clearValues, uint32_t clearCount)
                    {
                        VkRenderPassBeginInfo rpInfo{};
                        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                        rpInfo.renderPass = renderPass;
                        rpInfo.framebuffer = framebuffer;
                        rpInfo.renderArea.offset = {0, 0};
                        rpInfo.renderArea.extent = extent;
                        rpInfo.clearValueCount = clearCount;
                        rpInfo.pClearValues = clearValues;

                        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
                        return *this;
                    }

                    CommandList& EndRenderPass()
                    {
                        vkCmdEndRenderPass(cmd);
                        return *this;
                    }

                    CommandList& BuildAccelerationStructures(Fox::Scene::RayTracing::RayTracingScene& raytracingScene);

                    CommandList& BindPipeline(VkPipeline pipeline, VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS)
                    {
                        vkCmdBindPipeline(cmd, bindPoint, pipeline);
                        return *this;
                    }

                    CommandList& Raytrace(VkStridedDeviceAddressRegionKHR raygenRegion, VkStridedDeviceAddressRegionKHR missRegion, 
                        VkStridedDeviceAddressRegionKHR hitRegion, VkStridedDeviceAddressRegionKHR callableRegion, uint32_t width, uint32_t height) {

                        vkCmdTraceRaysKHR(
                            cmd,
                            &raygenRegion,
                            &missRegion,
                            &hitRegion,
                            &callableRegion,
                            width,
                            height,
                            1);

                        return *this;
                    }

                    CommandList& BindDescriptorSets(VkPipelineLayout layout,
                                                    uint32_t firstSet,
                                                    const std::vector<VkDescriptorSet>& sets,
                                                    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS)
                    {
                        vkCmdBindDescriptorSets(cmd, bindPoint, layout, firstSet,
                                                static_cast<uint32_t>(sets.size()), sets.data(),
                                                0, nullptr);
                        return *this;
                    }

                    CommandList& BindVertexBuffers(uint32_t firstBinding, const std::vector<VkBuffer>& buffers, const std::vector<VkDeviceSize>& offsets)
                    {
                        vkCmdBindVertexBuffers(cmd, firstBinding, static_cast<uint32_t>(buffers.size()), buffers.data(), offsets.data());
                        return *this;
                    }

                    CommandList& BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type)
                    {
                        vkCmdBindIndexBuffer(cmd, buffer, offset, type);
                        return *this;
                    }

                    CommandList& Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0)
                    {
                        vkCmdDraw(cmd, vertexCount, instanceCount, firstVertex, firstInstance);
                        return *this;
                    }

                    CommandList& DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0)
                    {
                        vkCmdDrawIndexed(cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
                        return *this;
                    }

                    CommandList& End()
                    {
                        if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
                            throw std::runtime_error("Failed to end command buffer!");
                        return *this;
                    }

                    CommandList& RenderMeshShader(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1)
                    {
                        vkCmdDrawMeshTasksEXT(cmd, groupCountX, groupCountY, groupCountZ);
                        return *this;
					}

                    static void RegisterRenderMeshShaderFunction(PFN_vkCmdDrawMeshTasksEXT funcPtr, PFN_vkCmdTraceRaysKHR raytracingFuncPointer)
                    {
                        vkCmdDrawMeshTasksEXT = funcPtr;
                        if (!vkCmdDrawMeshTasksEXT) {
                            std::cerr << "Could not load vkCmdDrawMeshTasksEXT" << std::endl;
                            exit(1);
                        }

                        vkCmdTraceRaysKHR = raytracingFuncPointer;
                    }

                    static void RegisterGetObjectName(PFN_vkDebugMarkerSetObjectNameEXT getName) {
                        vkGetObjectNameEXT = getName;
                    }

                    static void RegisterObjectNameFunction(PFN_vkSetDebugUtilsObjectNameEXT funcPtr)
                    {
                        vkSetDebugUtilsObjectNameEXT = funcPtr;
                        if (!vkSetDebugUtilsObjectNameEXT) {
                            std::cerr << "Could not load vkSetDebugUtilsObjectNameEXT" << std::endl;
                            exit(1);
                        }
					}

                    static void SetName(std::string name, uint64_t objectHandle, VkObjectType objectType, VkDevice device)
                    {
                        if (vkSetDebugUtilsObjectNameEXT) {
                            VkDebugUtilsObjectNameInfoEXT nameInfo{};
                            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                            nameInfo.objectType = objectType;
                            nameInfo.objectHandle = objectHandle;
                            nameInfo.pObjectName = name.c_str();
                            vkSetDebugUtilsObjectNameEXT(device, &nameInfo);
                        }
					}

                    static void PrintBufferNameAndAddress(VkDevice device, VkBuffer buffer, const std::string& name) {
                        Fox::Graphics::Vulkan::PrintBufferDetails(device, buffer, name);
                    }

                    CommandList& SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height, float minDepth = 0.0f, float maxDepth = 1.0f)
                    {
                        VkViewport viewport{};
                        viewport.x = static_cast<float>(x);
                        viewport.y = static_cast<float>(y);
                        viewport.width = static_cast<float>(width);
                        viewport.height = static_cast<float>(height);
                        viewport.minDepth = minDepth;
                        viewport.maxDepth = maxDepth;
                        vkCmdSetViewport(cmd, 0, 1, &viewport);
                        return *this;
					}

                    CommandList& SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
                    {
                        VkRect2D scissor{};
                        scissor.offset = { static_cast<int32_t>(x), static_cast<int32_t>(y) };
                        scissor.extent = { width, height };
                        vkCmdSetScissor(cmd, 0, 1, &scissor);
                        return *this;
					}

                    // --- Submission helpers ---
                    void Submit(VkQueue queue,
                                VkSemaphore waitSemaphore = VK_NULL_HANDLE,
                                VkSemaphore signalSemaphore = VK_NULL_HANDLE,
                                VkFence fence = VK_NULL_HANDLE,
                                VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)
                    {
                        VkSubmitInfo submitInfo{};
                        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                        VkCommandBuffer cb = cmd;
                        submitInfo.commandBufferCount = 1;
                        submitInfo.pCommandBuffers = &cb;

                        if (waitSemaphore != VK_NULL_HANDLE) {
                            submitInfo.waitSemaphoreCount = 1;
                            submitInfo.pWaitSemaphores = &waitSemaphore;
                            submitInfo.pWaitDstStageMask = &waitStage;
                        }

                        if (signalSemaphore != VK_NULL_HANDLE) {
                            submitInfo.signalSemaphoreCount = 1;
                            submitInfo.pSignalSemaphores = &signalSemaphore;
                        }
						VkResult result = vkQueueSubmit(queue, 1, &submitInfo, fence);
                        if (result != VK_SUCCESS) {
                            throw std::runtime_error("Failed to submit command buffer!");
                        }
                    }

                    void SubmitAndWait(VkQueue queue)
                    {
                        Submit(queue);
                        vkQueueWaitIdle(queue);
                    }

                    CommandList& CopyBuffer(VkBuffer src, VkBuffer dst, const std::vector<VkBufferCopy>& regions) {
                        vkCmdCopyBuffer(cmd, src, dst, static_cast<uint32_t>(regions.size()), regions.data());
                        return *this;
                    }

                    // Convenience overload: copy single region
                    CommandList& CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0) {
                        VkBufferCopy region{};
                        region.srcOffset = srcOffset;
                        region.dstOffset = dstOffset;
                        region.size = size;
                        vkCmdCopyBuffer(cmd, src, dst, 1, &region);
                        return *this;
                    }

                    // --- Buffer -> Image helper (for texture uploads) ---
                    // regions must be filled with VkBufferImageCopy describing each mip/array layer/etc.
                    CommandList& CopyBufferToImage(VkBuffer srcBuffer,
                        VkImage dstImage,
                        VkImageLayout dstImageLayout,
                        const std::vector<VkBufferImageCopy>& regions)
                    {
                        // The caller must ensure dstImage is in a layout that allows transfer dst,
                        // e.g. VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
                        vkCmdCopyBufferToImage(cmd,
                            srcBuffer,
                            dstImage,
                            dstImageLayout,
                            static_cast<uint32_t>(regions.size()),
                            regions.data());
                        return *this;
                    }

                    // Convenience overload for a single region
                    CommandList& CopyBufferToImage(VkBuffer srcBuffer,
                        VkImage dstImage,
                        VkImageLayout dstImageLayout,
                        const VkBufferImageCopy& region)
                    {
                        vkCmdCopyBufferToImage(cmd, srcBuffer, dstImage, dstImageLayout, 1, &region);
                        return *this;
                    }

                    CommandList& CopyImage(VkImage storageImage, VkImage swapchainImage, uint32_t width, uint32_t height) {
                        VkImageCopy copy{};
                        copy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                        copy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                        copy.extent = { width, height, 1 };

                        vkCmdCopyImage(
                            cmd,
                            storageImage,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            swapchainImage,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1,
                            &copy
                        );

                        return *this;
                    }

                    // --- Image layout transition helper (image memory barrier) ---
                    // Inserts an image memory barrier to transition oldLayout -> newLayout for the given subresourceRange.
                    // srcStageMask/dstStageMask are the pipeline stages that synchronize the transition (choose sensible defaults below).
                    CommandList& TransitionImageLayout(VkImage image,
                        VkImageLayout oldLayout,
                        VkImageLayout newLayout,
                        const VkImageSubresourceRange& subresourceRange,
                        VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
                    {
                        VkAccessFlags srcAccessMask = 0;
                        VkAccessFlags dstAccessMask = 0;

                        // Translate layout to typical access masks (common cases)
                        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
                            srcAccessMask = 0;
                        }
                        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                            srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        }
                        else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                            srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                        }
                        else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
                            srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                        }
                        else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                            srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                        } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL) {
                            srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                        }

                        if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                            dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                            // if not provided, choose stages accordingly
                            if (dstStageMask == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
                                dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
                        }
                        else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                            dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                            if (dstStageMask == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
                                dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                        }
                        else if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                            dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                            if (dstStageMask == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
                                dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                        }
                        else if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
                            dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                            if (dstStageMask == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
                                dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                        }
                        else if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
                            // presenting engine expects the image to be available for presentation
                            dstAccessMask = 0;
                            if (dstStageMask == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
                                dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                        } else if (newLayout == VK_IMAGE_LAYOUT_GENERAL) {
                            dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                        }

                        VkImageMemoryBarrier barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        barrier.oldLayout = oldLayout;
                        barrier.newLayout = newLayout;
                        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.image = image;
                        barrier.subresourceRange = subresourceRange;
                        barrier.srcAccessMask = srcAccessMask;
                        barrier.dstAccessMask = dstAccessMask;

                        // If caller didn't specify srcStageMask/dstStageMask sensibly, try to infer a common src stage:
                        if (srcStageMask == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT) {
                            // infer from srcAccessMask
                            if (barrier.srcAccessMask & VK_ACCESS_TRANSFER_WRITE_BIT) srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
                            else if (barrier.srcAccessMask & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT) srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                            else if (barrier.srcAccessMask & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                            else if (barrier.srcAccessMask & VK_ACCESS_SHADER_READ_BIT) srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                            else srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                        }

                        vkCmdPipelineBarrier(
                            cmd,
                            srcStageMask,
                            dstStageMask,
                            0,            // dependency flags
                            0, nullptr,   // memory barriers
                            0, nullptr,   // buffer barriers
                            1, &barrier   // image barriers
                        );

                        return *this;
                    }

                    // --- Buffer memory barrier helper ---
                    CommandList& BufferMemoryBarrier(VkBuffer buffer,
                        VkDeviceSize offset,
                        VkDeviceSize size,
                        VkAccessFlags srcAccessMask,
                        VkAccessFlags dstAccessMask,
                        VkPipelineStageFlags srcStageMask,
                        VkPipelineStageFlags dstStageMask)
                    {
                        VkBufferMemoryBarrier barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                        barrier.srcAccessMask = srcAccessMask;
                        barrier.dstAccessMask = dstAccessMask;
                        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.buffer = buffer;
                        barrier.offset = offset;
                        barrier.size = size;

                        vkCmdPipelineBarrier(
                            cmd,
                            srcStageMask,
                            dstStageMask,
                            0,
                            0, nullptr,
                            1, &barrier,
                            0, nullptr
                        );

                        return *this;
                    }

                    // --- Generic pipeline barrier allowing mixed barriers ---
                    CommandList& PipelineBarrier(VkPipelineStageFlags srcStageMask,
                        VkPipelineStageFlags dstStageMask,
                        const std::vector<VkMemoryBarrier>& memBarriers = {},
                        const std::vector<VkBufferMemoryBarrier>& bufferBarriers = {},
                        const std::vector<VkImageMemoryBarrier>& imageBarriers = {})
                    {
                        vkCmdPipelineBarrier(
                            cmd,
                            srcStageMask,
                            dstStageMask,
                            0,
                            static_cast<uint32_t>(memBarriers.size()), memBarriers.empty() ? nullptr : memBarriers.data(),
                            static_cast<uint32_t>(bufferBarriers.size()), bufferBarriers.empty() ? nullptr : bufferBarriers.data(),
                            static_cast<uint32_t>(imageBarriers.size()), imageBarriers.empty() ? nullptr : imageBarriers.data()
                        );
                        return *this;
                    }

                    VkCommandBuffer Get() const { return cmd; }
                    operator VkCommandBuffer() const { return cmd; }

                private:
                    VkDevice device = VK_NULL_HANDLE;
                    VkCommandPool pool = VK_NULL_HANDLE;
                    VkCommandBuffer cmd = VK_NULL_HANDLE;

                    static PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT;
                    static PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
                    static PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT;
                    static PFN_vkDebugMarkerSetObjectNameEXT vkGetObjectNameEXT;
                };

        }
    }
}