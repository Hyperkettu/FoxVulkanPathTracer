#pragma once 

namespace Fox {

	namespace Graphics {
	
		namespace Vulkan {
		
			struct Material {
				glm::vec4 albedo;
				glm::vec2 roughnessMetallic;
				uint32_t albedoTextureIndex;
				uint32_t roughnessMetallicTextureIndex;
			};
			
		}
	}

}