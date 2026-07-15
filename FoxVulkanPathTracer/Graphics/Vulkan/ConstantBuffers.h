
namespace Fox {

    namespace Graphics {

	    namespace Vulkan {

			struct CameraUBO {
				alignas(16) glm::vec3 camPos;       // Offset 0 (12 bytes + 4 bytes auto-padding)
				alignas(16) glm::vec3 camForward;   // Offset 16 (12 bytes + 4 bytes auto-padding)
				alignas(16) glm::vec3 camRight;     // Offset 32 (12 bytes + 4 bytes auto-padding)
				alignas(16) glm::vec3 camUp;        // Offset 48 (12 bytes + 4 bytes auto-padding)
				alignas(16) glm::vec2 resolution;   // Offset 64 (8 bytes)
				alignas(8)  uint32_t frameIndex;    // Offset 72 (4 bytes) - packs right after resolution!
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

			struct Environment {
				uint32_t environmentMapTextureIndex;
				float environmentMapIntensity;
				int32_t pad0;
				int32_t pad1;
			}; 
	    }
    }
}