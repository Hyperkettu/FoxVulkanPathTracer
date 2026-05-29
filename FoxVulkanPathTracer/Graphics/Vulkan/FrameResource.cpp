#include "FoxRenderer.h"

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            int32_t FrameResource::CreateGraphicsCommandResources(VkDevice device, uint32_t queueFamily) {
                commandPool = std::make_unique<Fox::Graphics::Vulkan::CommandPool>(device, queueFamily);
				commandList = std::make_unique<Fox::Graphics::Vulkan::CommandList>(device, commandPool->Get());
                offscreenCommandList = std::make_unique<Fox::Graphics::Vulkan::CommandList>(device, commandPool->Get());
                return 0;
            }

            int32_t FrameResource::CreateSynchronizationObjects(VkDevice device, bool createFenceAsSignaled) {
				imageAvailableSemaphore = std::make_unique<Fox::Graphics::Vulkan::Semaphore>(device);
				renderFinishedSemaphore = std::make_unique<Fox::Graphics::Vulkan::Semaphore>(device);   
                offscreenFinishedSemaphore = std::make_unique<Fox::Graphics::Vulkan::Semaphore>(device);
                renderFence = std::make_unique<Fox::Graphics::Vulkan::Fence>(device, createFenceAsSignaled);
                return 1;
            }

            void FrameResource::Destroy(VkDevice device) {
                oldPerFrameUBO = nullptr;
				perFrameUBO = nullptr;
				meshTransformsUBO = nullptr;
				meshInfosUBO = nullptr;
                commandList = nullptr;
                offscreenCommandList = nullptr;
				commandPool = nullptr;
				perFrameDescriptorSet = nullptr;
                offscreenDescriptorSet = nullptr;
				imageAvailableSemaphore = nullptr;
				renderFinishedSemaphore = nullptr;  
                offscreenFinishedSemaphore = nullptr;
				renderFence = nullptr;
            }
        }
    }
}