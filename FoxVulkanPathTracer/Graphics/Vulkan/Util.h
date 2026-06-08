#pragma once 

namespace Fox {

	namespace Graphics {
	
		namespace Vulkan {
		
			void PrintBufferDetails(VkDevice device, VkBuffer buffer, const std::string& fallbackName = "Unknown");
		
		}

	}

}