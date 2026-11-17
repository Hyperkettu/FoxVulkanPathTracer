#pragma once

namespace Fox
{
    namespace Graphics {

        namespace Vulkan {

            // Everything that must exist once per frame in flight, so the CPU can record
            // frame N+1 while the GPU is still working on frame N.
            struct FrameResource
            {
                FrameResource() = default;

                void CreateGraphicsCommandResources(VkDevice device, uint32_t queueFamily);
                void CreateSynchronizationObjects(VkDevice device, bool createFenceAsSignaled);
                void CreateConstantBuffers(VkDevice device, VkPhysicalDevice physicalDevice);
                void Destroy(VkDevice device);

                // --- Synchronization ---
                std::unique_ptr<Fox::Graphics::Vulkan::Fence> renderFence;                  // Signaled when the GPU finished this frame
                std::unique_ptr<Fox::Graphics::Vulkan::Semaphore> imageAvailableSemaphore;  // Signaled when the swapchain image is ready

                // --- Command recording ---
                std::unique_ptr<Fox::Graphics::Vulkan::CommandPool> commandPool;
                std::unique_ptr<Fox::Graphics::Vulkan::CommandList> commandList;        // Path tracing and denoising
                std::unique_ptr<Fox::Graphics::Vulkan::CommandList> presentCommandList; // Tone mapping into the swapchain image

                // --- Descriptors ---
                std::unique_ptr<Fox::Graphics::Vulkan::DescriptorSet> perFrameDescriptorSet; // Ray tracing set 0

                // --- Per-frame uniform data ---
                std::unique_ptr<Fox::Graphics::Vulkan::ConstantBuffer<Fox::Graphics::Vulkan::CameraUBO>> camUBO;

                uint32_t frameIndex = 0;
            };
        }
    }
}
