#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			struct Vertex
			{
				float position[4];
				float normal[4];
				float uv[2];
				float pad[2];
				float tangent[4]; // xyz + handedness (w); all zero when the mesh has no tangents
			};

			// Must match the Vertex struct in Shaders/closest_hit.slang
			static_assert(sizeof(Vertex) == 64, "Vertex layout must match closest_hit.slang");

			struct Submesh
			{
				uint32_t vertexOffset = 0;
				uint32_t indexOffset = 0;
				uint32_t vertexCount = 0;
				uint32_t indexCount = 0;
				uint32_t materialIndex = 0;
				uint32_t previousPositions = 0; // Skinned: 1 + its first entry of the previous positions buffer; 0 = not skinned
				uint32_t pad1 = 0;
				uint32_t pad2 = 0;
			};
		}
	}
}