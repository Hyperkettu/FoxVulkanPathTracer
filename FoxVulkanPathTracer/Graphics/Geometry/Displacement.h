#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "FoxVulkanPathTracer/Core/Loaders/GLTFLoader.h"

namespace Fox {

	namespace Graphics {

		namespace Geometry {

			// Displacement mapping: surfaces are subdivided into small triangles when a model is loaded
			// and every vertex is moved along its normal by a height map. The result is real geometry,
			// so silhouettes, shadows and reflections all see the relief (unlike normal mapping).
			//
			// glTF has no slot for displacement maps, so the heights come from one of two places:
			//   1. A height map file assigned to a material by name (heightMaps below).
			//   2. Reconstructed from the material's normal map, for models that ship without height maps.
			struct DisplacementSettings {
				bool enabled = false;

				// Triangles are split until no edge is longer than this (model units, usually meters).
				// Smaller = finer relief and more triangles. This is the finest edge length used.
				float targetEdgeLength = 0.03f;

				// Upper bound for the triangle count of the displaced surfaces of one model. When the target
				// edge length would exceed it, the budget goes where it buys the most visible relief: each
				// surface gets an edge length between the target and 16x the target.
				uint32_t maxTriangles = 4'000'000;

				// --- Height maps provided as files ---
				struct HeightMap {
					std::string file;      // Any format stb_image reads; the red channel is used (8 or 16 bit)
					float scale = 0.05f;   // Height difference between black and white, in model units
					float midlevel = 0.5f; // The map value that leaves the surface where it is
				};

				// Key: glTF material name
				std::unordered_map<std::string, HeightMap> heightMaps;

				// --- Heights reconstructed from normal maps ---
				// Used for materials that have a normal map but no entry in heightMaps.
				bool deriveFromNormalMaps = true;

				// Multiplier for reconstructed heights. 1 = the relief the normal map physically describes.
				float derivedStrength = 1.0f;

				// Reconstructed reliefs are scaled down so they never move a vertex further than this.
				float derivedMaxHeight = 0.06f;
			};

			struct DisplacementStats {
				uint32_t displacedMaterials = 0;
				size_t trianglesBefore = 0; // Of the displaced surfaces only
				size_t trianglesAfter = 0;
				float edgeLength = 0.0f;    // Finest edge length used
			};

			// Tessellates and displaces the surfaces of a loaded model in place. All arrays are the outputs of
			// GLTFLoader::LoadGLTFBinaryFile; submeshes and instances are parallel (one entry per primitive).
			DisplacementStats ApplyDisplacement(
				const DisplacementSettings& settings,
				std::vector<Fox::Core::Loaders::GLTF::VertexData>& vertices,
				std::vector<uint32_t>& indices,
				std::vector<Fox::Core::Loaders::GLTF::SubmeshData>& submeshes,
				std::vector<Fox::Graphics::Vulkan::RayTracing::RayTracingInstance>& instances,
				const std::vector<Fox::Core::Loaders::GLTF::MaterialData>& materials,
				const std::vector<Fox::Core::Loaders::GLTF::TextureData>& textures);
		}
	}
}
