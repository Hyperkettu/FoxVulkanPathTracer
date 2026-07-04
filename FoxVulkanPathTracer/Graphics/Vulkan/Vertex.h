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
			};

			struct Submesh
			{
				uint32_t vertexOffset = 0;
				uint32_t indexOffset = 0;
				uint32_t vertexCount = 0;
				uint32_t indexCount = 0;
			};
		}
	}
}