#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			struct Vertex {
				glm::vec3 position;
				glm::vec3 normal;
				glm::vec2 uv;

				// --- Binding description: how large each vertex is and how often it's stepped ---
		//		static VkVertexInputBindingDescription GetBindingDescription() {
		//			VkVertexInputBindingDescription bindingDesc{};
		//			bindingDesc.binding = 0;                       // binding index in shader
		//			bindingDesc.stride = sizeof(Vertex);           // size of each vertex
		//			bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // per-vertex data
		//			return bindingDesc;
		//		}

				// --- Attribute descriptions: describe layout of each attribute ---
		//		static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions() {
		//7			std::array<VkVertexInputAttributeDescription, 3> attributes{};

					// position (location = 0)
	//				attributes[0].binding = 0;
	//				attributes[0].location = 0;
	//				attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	//				attributes[0].offset = offsetof(Vertex, position);

					// normal (location = 1)
			//		attributes[1].binding = 0;
			//		attributes[1].location = 1;
			//		attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
			//		attributes[1].offset = offsetof(Vertex, normal);

					// uv (location = 2)
			//		attributes[2].binding = 0;
			//		attributes[2].location = 2;
			//		attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
			//		attributes[2].offset = offsetof(Vertex, uv);

//					return attributes;
//				}
			};

		}
	}
}