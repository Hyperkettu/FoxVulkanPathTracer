#pragma once

#include <vector>

namespace Fox {

	namespace Graphics {

		namespace Geometry {

			class GeometryGenerator {
			public:
				static void GenerateIcosahedron(std::vector<Fox::Graphics::Vulkan::Vertex>& outVertices, std::vector<uint32_t>& outIndices, Fox::Graphics::Vulkan::MeshInfo& outMeshInfo);
				static void GenerateCube(std::vector<Fox::Graphics::Vulkan::Vertex>& outVertices, std::vector<uint32_t>& outIndices, Fox::Graphics::Vulkan::MeshInfo& outMeshInfo);
				static void GenerateSphere(
					std::vector<Fox::Graphics::Vulkan::Vertex>& outVertices,
					std::vector<uint32_t>& outIndices,
					Fox::Graphics::Vulkan::MeshInfo& outMeshInfo,
					uint32_t sliceCount = 6, // 32,
					uint32_t stackCount = 6, // 16
					float radius = 1.0f);

				static void GenerateSphereFlipped(
					std::vector<Fox::Graphics::Vulkan::Vertex>& outVertices,
					std::vector<uint32_t>& outIndices,
					std::vector<Fox::Graphics::Vulkan::MeshInfo>& outMeshInfos,
					uint32_t sliceCount = 6,
					uint32_t stackCount = 6,
					float radius = 1.0f);

				static Fox::Graphics::Geometry::Vulkan::Mesh GeneratePlaneMesh(uint32_t xSegments, uint32_t ySegments, float dimension, float tileScale, float tileOffset);
			};
		} 
	} 
} 