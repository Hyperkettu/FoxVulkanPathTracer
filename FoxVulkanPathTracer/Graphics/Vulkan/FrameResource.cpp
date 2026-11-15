#include "FoxRenderer.h"

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            void FrameResource::CreateGraphicsCommandResources(VkDevice device, uint32_t queueFamily) {
                commandPool = std::make_unique<Fox::Graphics::Vulkan::CommandPool>(device, queueFamily);
                commandList = std::make_unique<Fox::Graphics::Vulkan::CommandList>(device, commandPool->Get());
            }

            void FrameResource::CreateSynchronizationObjects(VkDevice device, bool createFenceAsSignaled) {
                imageAvailableSemaphore = std::make_unique<Fox::Graphics::Vulkan::Semaphore>(device);
                renderFence = std::make_unique<Fox::Graphics::Vulkan::Fence>(device, createFenceAsSignaled);
            }

            void FrameResource::CreateConstantBuffers(VkDevice device, VkPhysicalDevice physicalDevice) {
                camUBO = std::make_unique<Fox::Graphics::Vulkan::ConstantBuffer<Fox::Graphics::Vulkan::CameraUBO>>(device, physicalDevice, "Camera UBO");
            }

            void FrameResource::Destroy(VkDevice device) {
                camUBO = nullptr;
                commandList = nullptr;
                commandPool = nullptr;
                perFrameDescriptorSet = nullptr;
                imageAvailableSemaphore = nullptr;
                renderFence = nullptr;
            }
        }
    }
}
