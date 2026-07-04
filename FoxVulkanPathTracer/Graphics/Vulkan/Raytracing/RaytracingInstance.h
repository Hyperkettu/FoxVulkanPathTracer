#pragma once 

namespace Fox {
	
	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				struct RayTracingInstance {
					uint32_t firstIndex;
					uint32_t indexCount;
					uint32_t materialIndex;
					VkTransformMatrixKHR transform; // Transposed 3x4 matrix for Vulkan Acceleration Structures
				};
			}
		}

	}
}