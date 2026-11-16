#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			// GPU material record. Plain 4-byte members only, so the layout is identical in C++ and
			// in the Material struct of Shaders/closest_hit.slang - keep the two in sync.
			struct Material {
				float baseColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

				float emissive[3] = { 0.0f, 0.0f, 0.0f }; // Emitted radiance (factor * strength)
				float normalScale = 1.0f;

				float roughness = 1.0f;
				float metallic = 0.0f;
				float transmission = 0.0f;   // 1 = thin clear glass
				float specularWeight = 1.0f; // Strength of the dielectric specular reflection

				// Indices into the bindless texture array, -1 if unused
				int baseColorTexture = -1;
				int metallicRoughnessTexture = -1;
				int normalTexture = -1;
				int emissiveTexture = -1;

				float uvScale[2] = { 1.0f, 1.0f };
				float uvRotation = 0.0f;
				int lightIndex = -1; // Index into the scene's light buffer if the surface is also sampled as a light
			};

			static_assert(sizeof(Material) == 80, "Material layout must match closest_hit.slang");
		}
	}
}
