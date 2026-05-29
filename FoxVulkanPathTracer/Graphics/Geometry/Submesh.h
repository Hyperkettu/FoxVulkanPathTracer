#pragma once

namespace Fox {
	
	namespace Graphics {
		
		namespace Geometry {

			struct Submesh
			{
				uint32_t vertexOffset = 0;
				uint32_t indexOffset = 0;
				uint32_t vertexCount = 0;
				uint32_t indexCount = 0;

				// Limit constants
				static constexpr uint32_t MaxVertices = 256;
				static constexpr uint32_t MaxTriangles = 84;
				static constexpr uint32_t MaxIndices = MaxTriangles * 3;

				bool IsFull() const
				{
					return vertexCount >= MaxVertices || (indexCount / 3) >= MaxTriangles;
				}

				void AddTriangle(uint32_t i0, uint32_t i1, uint32_t i2)
				{
					indexCount += 3;
				}
			};
	
		}	

	}
}
