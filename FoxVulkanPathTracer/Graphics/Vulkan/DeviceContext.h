#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			// The handles a subsystem needs to create GPU resources and run one-off setup commands.
			// Passed by value; it does not own anything.
			struct DeviceContext {
				VkInstance instance = VK_NULL_HANDLE;
				VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
				VkDevice device = VK_NULL_HANDLE;
				VkQueue graphicsQueue = VK_NULL_HANDLE;
				uint32_t graphicsQueueFamily = 0;
				VkCommandPool setupCommandPool = VK_NULL_HANDLE; // For immediate (submit-and-wait) commands
			};

		}
	}
}
