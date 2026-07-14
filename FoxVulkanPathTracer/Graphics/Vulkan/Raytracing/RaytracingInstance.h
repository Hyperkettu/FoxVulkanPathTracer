#pragma once 

#include <glm/glm.hpp>

namespace Fox {
	
	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				struct RayTracingInstance {
					uint32_t firstIndex;
					uint32_t indexCount;
					uint32_t materialIndex;
					glm::mat4 transform; // Transposed 3x4 matrix for Vulkan Acceleration Structures
				};
			}
		}

	}
}