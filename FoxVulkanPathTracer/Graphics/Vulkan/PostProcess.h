#pragma once

#include <vector>
#include "FoxVulkanPathTracer/Graphics/Vulkan/DescriptorSet.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/ConstantBuffers.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			class PostProcessor {
			public:
				PostProcessor(VkDevice device, VkPhysicalDevice physicalDevice) : device(device), physicalDevice(physicalDevice) {
				}

				virtual ~PostProcessor() = default;
				virtual void Process(Fox::Graphics::Vulkan::CommandList& commandList, VkSurfaceCapabilitiesKHR capabilities, VkQueue graphicsQueue, VkSemaphore renderFinishedSemaphore, std::unique_ptr<Fox::Graphics::Vulkan::Swapchain>& swapchain, uint32_t imageIndex);

			private:

				VkDevice device;
				VkPhysicalDevice physicalDevice;
			};

		}
	}
}