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

			// How far along the rig each bone starts: metres from the root, along the bones it grows out of (a bone
			// starting partway along its parent adds that part). Bones come after their parents.
			inline std::vector<float> DistancesAlongRig(const std::vector<Bone>& bones) {
				std::vector<float> distances(bones.size(), 0.0f);
				for (size_t i = 0; i < bones.size(); i++) {
					const int parent = bones[i].parent;
					if (parent < 0) {
						continue;
					}
					const Bone& p = bones[parent];
					const glm::vec3 axis = p.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
					distances[i] = distances[parent] + glm::clamp(glm::dot(bones[i].origin - p.origin, axis), 0.0f, p.length);
				}
				return distances;
			}

			// How far along the rig a point on a bone is
			inline float DistanceAlongRig(const std::vector<Bone>& bones, const std::vector<float>& distances, int bone, const glm::vec3& point) {
				if (bone < 0 || bone >= static_cast<int>(bones.size())) {
					return 0.0f;
				}
				const Bone& b = bones[bone];
				const glm::vec3 axis = b.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
				return distances[bone] + glm::clamp(glm::dot(point - b.origin, axis), 0.0f, b.length);
			}

			// A mesh whose vertices follow a rig: vertex v is placed by sum_j weight_j * skin[joint_j] * v, where
			// skin[j] = pose[j] * inverse(bind[j]) (see RayTracingScene::SetJointMatrices)
			struct SkinnedMesh {
				std::vector<Fox::Graphics::Vulkan::Vertex> vertices; // Bind pose
				std::vector<uint32_t> indices;
				std::vector<SkinWeights> skin;                       // Per vertex
				std::vector<Bone> bones;
				// Growing (trees, see Animation::TreeGrowth), per vertex or empty: xyz where it grows from (a leaf
				// card's stem, bind pose), w = 1 + how far along the tree that is (metres from the root along its
				// bones); w = 0: it grows with its bones only (bark)
				std::vector<glm::vec4> growth;

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
