#pragma once

#include <algorithm>
#include <cmath>

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			// GPU material record. Plain 4-byte members only, so the layout is identical in C++ and
			// in the Material struct of Shaders/common.slang - keep the two in sync.
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

				// Transmissive materials (transmission > 0) are thin walled by default: light passes straight
				// through, like a window pane. With an index of refraction they are solid instead: the surface
				// encloses glass, light refracts where it enters and leaves, and the glass absorbs on the way.
				// Solids must be closed meshes with outward facing normals.
				float ior = 0.0f; // 0 = thin walled. Glass: 1.5, water: 1.33, diamond: 2.42
				float absorption[3] = { 0.0f, 0.0f, 0.0f }; // Per meter inside a solid, see SetAttenuation

				// KHR_materials_volume: light keeps `color` of its intensity after travelling `distance`
				// meters inside the material. distance <= 0 means no absorption.
				void SetAttenuation(const float color[3], float distance) {
					for (int c = 0; c < 3; c++) {
						absorption[c] = distance > 0.0f ? -std::log(std::max(color[c], 1e-4f)) / distance : 0.0f;
					}
				}

				// Ambient occlusion (red channel), as in glTF: darkens light arriving from the surroundings (sky,
				// other surfaces) in crevices finer than the geometry. Direct light from the sun is not affected.
				int occlusionTexture = -1;
				float occlusionStrength = 1.0f; // 0 = ignore the texture
				float padding[2] = { 0.0f, 0.0f };

				bool IsSolidGlass() const { return transmission > 0.5f && ior > 0.0f; }
				bool IsThinGlass() const { return transmission > 0.5f && ior <= 0.0f; }
			};

			static_assert(sizeof(Material) == 112, "Material layout must match common.slang");
		}
	}
}
