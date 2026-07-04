#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			struct Vertex
			{
				float position[4]; // pad to 16 bytes
				float normal[4];   // pad to 16 bytes
				float uv[2];
				float pad[2];
			};

			struct Index3 {
				uint32_t x, y, z;
			};

			struct MeshGPU {
				VkBuffer vertexBuffer;
				VkBuffer indexBuffer;
				uint32_t indexCount;
			};

			static_assert(sizeof(Index3) == 12, "Index3 size must be exactly 12 bytes to match Slang's uint3!");
		//	static_assert(sizeof(Vertex) == 32, "CRITICAL: C++ Vertex size must be exactly 32 bytes to match the shader layout!");
		}
	}
}