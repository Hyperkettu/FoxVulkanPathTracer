#pragma once 

namespace Fox {

	namespace Graphics {
	
		namespace Vulkan {
		
			struct Material {
				glm::vec4 albedo;
				glm::vec2 roughnessMetallic;
				uint32_t albedoTextureIndex;
				uint32_t roughnessMetallicTextureIndex;
				glm::vec3 emissiveColor = { 0.0f, 0.0f, 0.0f };
				int emissiveTextureIndex = -1;
				float intensity = 1.0f;
				float padding = 0.0f;
			};
			
		}
	}

}