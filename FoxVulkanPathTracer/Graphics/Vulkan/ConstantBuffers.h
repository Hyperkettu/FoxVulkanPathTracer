#pragma once

#include <cstddef>


namespace Fox {

    namespace Graphics {

	    namespace Vulkan {

			struct CameraUBO {
				alignas(16) glm::vec3 camPos;       // Offset 0 (12 bytes + 4 bytes auto-padding)
				alignas(16) glm::vec3 camForward;   // Offset 16 (12 bytes + 4 bytes auto-padding)
				alignas(16) glm::vec3 camRight;     // Offset 32 (12 bytes + 4 bytes auto-padding)
				alignas(16) glm::vec3 camUp;        // Offset 48 (12 bytes + 4 bytes auto-padding)

				// Previous frame, for motion vectors
				alignas(16) glm::vec3 prevCamPos;     // Offset 64
				alignas(16) glm::vec3 prevCamForward; // Offset 80
				alignas(16) glm::vec3 prevCamRight;   // Offset 96
				alignas(16) glm::vec3 prevCamUp;      // Offset 112

				alignas(16) glm::vec2 resolution;   // Offset 128
				alignas(8)  glm::vec2 jitter;       // Offset 136 - sub-pixel offset of the primary rays, in pixels
				alignas(4)  uint32_t frameIndex;    // Offset 144
				alignas(4)  uint32_t lightCount;    // Offset 148 - number of valid entries in the light buffer
				alignas(4)  float tanHalfFov;       // Offset 152 - tan(vertical field of view / 2)
				alignas(4)  uint32_t samplesPerPixel;  // Offset 156 - paths traced per pixel and frame
				alignas(4)  uint32_t jitterAllSamples; // Offset 160 - 1: all paths of a pixel share the jitter
				alignas(4)  uint32_t gbufferFollowsReflections; // Offset 164 - 1: describe what a mirror shows, not the mirror
				alignas(8)  glm::ivec2 pickPixel{ -1, -1 }; // Offset 168 - report the object seen at this pixel (editor)
				alignas(4)  uint32_t pickSlot;      // Offset 176 - entry of the pick result buffer to write
			};

			// Must match the Camera struct in Shaders/common.slang (std140 layout).
			static_assert(offsetof(CameraUBO, prevCamPos) == 64, "CameraUBO layout must match common.slang");
			static_assert(offsetof(CameraUBO, resolution) == 128, "CameraUBO layout must match common.slang");
			static_assert(offsetof(CameraUBO, jitter) == 136, "CameraUBO layout must match common.slang");
			static_assert(offsetof(CameraUBO, frameIndex) == 144, "CameraUBO layout must match common.slang");
			static_assert(offsetof(CameraUBO, lightCount) == 148, "CameraUBO layout must match common.slang");
			static_assert(offsetof(CameraUBO, jitterAllSamples) == 160, "CameraUBO layout must match common.slang");
			static_assert(offsetof(CameraUBO, gbufferFollowsReflections) == 164, "CameraUBO layout must match common.slang");
			static_assert(offsetof(CameraUBO, pickPixel) == 168, "CameraUBO layout must match common.slang");
			static_assert(offsetof(CameraUBO, pickSlot) == 176, "CameraUBO layout must match common.slang");

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

			// Must match the Environment struct in Shaders/lighting.slang
			struct Environment {
				uint32_t environmentMapTextureIndex;
				float environmentMapIntensity;
				uint32_t tableWidth;  // Resolution of the importance sampling table
				uint32_t tableHeight;
			};

			// One entry of the environment importance sampling (alias) table.
			// Must match the EnvironmentCell struct in Shaders/lighting.slang
			struct EnvironmentCell {
				float threshold;   // Probability of keeping this cell instead of jumping to alias
				uint32_t alias;
				float probability; // Chance that sampling ends up in this cell
				float pad;
			};
	    }
    }
}