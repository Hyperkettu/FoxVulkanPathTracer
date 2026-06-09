#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {
	
		namespace Vulkan {
		
            void PrintBufferDetails(VkDevice device, VkBuffer buffer, const std::string& fallbackName) {
                // 1. Get Handle in Hex
                // VkBuffer is a uint64_t or pointer depending on platform/config
                uint64_t handleHex = reinterpret_cast<uint64_t>(buffer);

                // 2. Get Device Address in Hex (GPU memory pointer)
                VkBufferDeviceAddressInfo info{};
                info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                info.buffer = buffer;

                // Note: vkGetBufferDeviceAddress requires the buffer to be created with 
                // VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                VkDeviceAddress deviceAddress = vkGetBufferDeviceAddress(device, &info);

                // 3. Print everything beautifully
                std::cout << "=== Vulkan Buffer Info ===" << std::endl;
                std::cout << "Name:           " << fallbackName << std::endl;
                std::cout << "Handle:         0x" << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << handleHex << std::endl;
                if (deviceAddress != 0) {
                    std::cout << "Device Address: 0x" << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << deviceAddress << std::endl;
                }
                else {
                    std::cout << "Device Address: N/A (Missing VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)" << std::endl;
                }
                std::cout << "--------------------------" << std::endl;

                // Reset cout stream formatting back to normal/dec
                std::cout << std::dec << std::nouppercase;
            }

            VkDeviceAddress GetBufferDeviceAddress(VkDevice device, VkBuffer buffer)
            {
                VkBufferDeviceAddressInfo info{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                    .buffer = buffer
                };
                return vkGetBufferDeviceAddress(device, &info);
            }
		
		}

	}

}