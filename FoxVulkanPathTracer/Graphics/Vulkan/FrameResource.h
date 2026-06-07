namespace Fox 
{
    namespace Graphics {

        namespace Vulkan {

            struct FrameResource
            {
				FrameResource() = default; 

                int32_t CreateGraphicsCommandResources(VkDevice device, uint32_t queueFamily);
                int32_t CreateSynchronizationObjects(VkDevice device, bool createFenceAsSignaled);
                void Destroy(VkDevice device);

                int32_t CreateConstantBuffers(VkDevice device, VkPhysicalDevice physicalDevice) {
                    oldPerFrameUBO = std::make_unique<Fox::Graphics::Vulkan::ConstantBuffer<Fox::Graphics::Vulkan::OldFrame>>(device, physicalDevice);
                    perFrameUBO = std::make_unique<Fox::Graphics::Vulkan::ConstantBuffer<Fox::Graphics::Vulkan::PerFrame>>(device, physicalDevice);
                    camUBO = std::make_unique<Fox::Graphics::Vulkan::ConstantBuffer<Fox::Graphics::Vulkan::CameraUBO>>(device, physicalDevice);

                    meshTransformsUBO = std::make_unique<Fox::Graphics::Vulkan::DynamicConstantBuffer<glm::mat4>>(device, physicalDevice);
                    meshInfosUBO = std::make_unique<Fox::Graphics::Vulkan::DynamicBuffer<Fox::Graphics::Vulkan::MeshInfo>>(device, physicalDevice);

                    return 1;
				}

                // --- Synchronization ---
                std::unique_ptr<Fox::Graphics::Vulkan::Fence> renderFence; // Fence: signals when frame is done rendering
                std::unique_ptr<Fox::Graphics::Vulkan::Semaphore> imageAvailableSemaphore;// Signals when swapchain image is ready
                std::unique_ptr<Fox::Graphics::Vulkan::Semaphore>  renderFinishedSemaphore;// Signals when rendering is finished
                std::unique_ptr<Fox::Graphics::Vulkan::Semaphore> offscreenFinishedSemaphore;

                // --- Command recording ---
                std::unique_ptr<Fox::Graphics::Vulkan::CommandPool> commandPool;         // Per-frame command pool
                std::unique_ptr<Fox::Graphics::Vulkan::CommandList> commandList;      // Main primary command buffer
                std::unique_ptr<Fox::Graphics::Vulkan::CommandList> offscreenCommandList;

                // --- Descriptor management ---
                std::unique_ptr<Fox::Graphics::Vulkan::DescriptorSet> offscreenDescriptorSet;
                std::unique_ptr<Fox::Graphics::Vulkan::DescriptorSet> perFrameDescriptorSet;

                // --- Per-frame uniform/constant data ---
                std::unique_ptr<Fox::Graphics::Vulkan::ConstantBuffer<Fox::Graphics::Vulkan::OldFrame>> oldPerFrameUBO;
                std::unique_ptr<Fox::Graphics::Vulkan::ConstantBuffer<Fox::Graphics::Vulkan::PerFrame>> perFrameUBO;
                std::unique_ptr<Fox::Graphics::Vulkan::ConstantBuffer<Fox::Graphics::Vulkan::CameraUBO>> camUBO;
                
                std::unique_ptr<Fox::Graphics::Vulkan::DynamicConstantBuffer<glm::mat4>> meshTransformsUBO;
				std::unique_ptr<Fox::Graphics::Vulkan::DynamicBuffer<Fox::Graphics::Vulkan::MeshInfo>> meshInfosUBO;

                std::unique_ptr<Fox::Graphics::Vulkan::StorageTexture> storageTexture;

                // --- Frame index tracking ---
                uint32_t frameIndex;
            };
        }
    }
}

