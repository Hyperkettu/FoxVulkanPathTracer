
namespace Fox {

    namespace Graphics {

	    namespace Vulkan {

			struct CameraUBO {
				alignas(16) glm::vec3 camPos;
				alignas(16) glm::vec3 camForward;
				alignas(16) glm::vec3 camRight;
				alignas(16) glm::vec3 camUp;
				alignas(16) glm::vec2 resolution;
			};

			struct OldFrame {
				alignas(16) glm::mat4 model;
				alignas(16) glm::mat4 view;
				alignas(16) glm::mat4 proj;
			};

		    struct PerFrame {
			    alignas(16) glm::mat4 view;
			    alignas(16) glm::mat4 proj;
		    };

			struct MeshTransforms {
				std::vector<glm::mat4> models;
			};

			struct MeshInfo {
				uint32_t vertexOffset;
				uint32_t indexOffset;
				uint32_t indexCount;
				uint32_t modelIndex;
			};

			struct MeshInfos {
				std::vector<MeshInfo> meshInfos;
			};
	    }
    }
}