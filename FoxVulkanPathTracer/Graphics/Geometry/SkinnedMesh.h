#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Fox {

	namespace Graphics {

		namespace Geometry {

			// Up to four joints influence a vertex; the weights add up to 1
			struct SkinWeights {
				uint32_t joints[4] = { 0, 0, 0, 0 };
				float weights[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
			};

			// A joint of the rig in its bind pose (the pose the mesh was built in), in mesh space.
			// The bone starts at origin and runs along the +Y axis of rotation for length.
			struct Bone {
				int parent = -1; // Bones come after their parents
				glm::vec3 origin{ 0.0f };
				glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
				float length = 1.0f;
				float radius = 0.1f; // Thickness of what it carries (branches): stiffness, mass and wind load

				glm::mat4 BindMatrix() const {
					return glm::translate(glm::mat4(1.0f), origin) * glm::mat4_cast(rotation);
				}
			};

			// A mesh whose vertices follow a rig: vertex v is placed by sum_j weight_j * skin[joint_j] * v, where
			// skin[j] = pose[j] * inverse(bind[j]) (see RayTracingScene::SetJointMatrices)
			struct SkinnedMesh {
				std::vector<Fox::Graphics::Vulkan::Vertex> vertices; // Bind pose
				std::vector<uint32_t> indices;
				std::vector<SkinWeights> skin;                       // Per vertex
				std::vector<Bone> bones;

				// Ranges of indices with their own material (e.g. bark and leaves). Empty: one part, all indices.
				struct Part {
					uint32_t firstIndex = 0;
					uint32_t indexCount = 0;
					uint32_t material = 0; // Into the materials given with the mesh
				};
				std::vector<Part> parts;
			};
		}
	}
}
