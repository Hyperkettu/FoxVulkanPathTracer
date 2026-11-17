#include "FoxRenderer.h"

#include <random>

namespace Fox {

	namespace Graphics {

		namespace Geometry {

			namespace {
				const glm::vec3 UP(0.0f, 1.0f, 0.0f);

				// Per branch level: cylinders along the branch, and vertices around it
				const int CYLINDERS[] = { 6, 4, 3, 2 };
				const int RADIAL[] = { 16, 12, 8, 6 };

				const float GOLDEN_ANGLE = 2.39996323f; // Spreads branches evenly around their parent
				const float MIN_RADIUS = 0.008f;

				glm::vec3 AnyPerpendicular(const glm::vec3& direction) {
					const glm::vec3 other = std::abs(direction.y) < 0.9f ? UP : glm::vec3(1.0f, 0.0f, 0.0f);
					return glm::normalize(glm::cross(direction, other));
				}

				class TreeBuilder {
				public:
					TreeBuilder(const TreeSettings& settings) : settings(settings), trunkRadius(std::max(settings.trunkRadius, 1e-3f)), random(settings.seed) {}

					// One branch (the trunk is the first): a chain of cylinders, then the branches growing out of it.
					// reference: a direction across the parent, so rings line up with it (no twist at the base).
					void Grow(int parentBone, glm::vec3 start, glm::vec3 direction, float length, float radiusBase, float radiusTip, int level, glm::vec3 reference) {
						const int cylinders = CYLINDERS[std::min(level, 3)];
						const float cylinderLength = length / static_cast<float>(cylinders);

						// Displaced branches are subdivided finely enough to carry the relief
						const bool displaced = static_cast<bool>(settings.height) && radiusBase >= settings.minDisplacedRadius;
						const float circumference = glm::two_pi<float>() * radiusBase;
						const int radial = displaced
							? std::clamp(static_cast<int>(std::round(circumference / settings.detailEdgeLength)), RADIAL[std::min(level, 3)], 160)
							: RADIAL[std::min(level, 3)];
						const int rings = displaced
							? std::max(settings.ringsPerCylinder, static_cast<int>(std::ceil(cylinderLength / settings.detailEdgeLength)))
							: std::max(settings.ringsPerCylinder, 1);

						// Texture repeats around the branch: a whole number, so the seam matches. Relief is scaled down
						// on thinner branches (bark texture is made for a trunk).
						const float repeatsAround = std::max(1.0f, std::round(circumference / settings.textureSize));
						const float relief = settings.displacementScale * std::min(1.0f, radiusBase / trunkRadius);
						const float handedness = settings.normalMapGreenUp ? -1.0f : 1.0f;

						// --- Rig: one bone per cylinder ---
						std::vector<glm::vec3> starts(cylinders + 1);
						std::vector<glm::vec3> directions(cylinders);
						std::vector<int> bones(cylinders);

						glm::vec3 position = start;
						glm::vec3 heading = glm::normalize(direction);
						for (int c = 0; c < cylinders; c++) {
							if (c > 0) {
								// Wander a little, and turn upwards (the trunk barely)
								const float upward = level > 0 ? settings.upwardTendency : 0.02f;
								heading = glm::normalize(heading + RandomUnit() * settings.bendiness + UP * upward);
							}

							directions[c] = heading;
							starts[c] = position;

							Bone bone;
							bone.parent = c == 0 ? parentBone : bones[c - 1];
							bone.origin = position;
							bone.rotation = glm::rotation(UP, heading);
							bone.length = cylinderLength;
							bone.radius = glm::mix(radiusBase, radiusTip, (static_cast<float>(c) + 0.5f) / static_cast<float>(cylinders));

							bones[c] = static_cast<int>(mesh.bones.size());
							mesh.bones.push_back(bone);

							position += heading * cylinderLength;
						}
						starts[cylinders] = position;

						// --- Geometry: the cylinders, batched into the mesh ---

						// Where two cylinders meet, the shared ring faces halfway between them
						auto jointDirection = [&](int joint) {
							if (joint == 0) return directions[0];
							if (joint == cylinders) return directions[cylinders - 1];
							return glm::normalize(directions[joint - 1] + directions[joint]);
						};

						// Ring orientation, carried along the branch without twisting
						glm::vec3 across = reference - directions[0] * glm::dot(reference, directions[0]);
						across = glm::dot(across, across) > 1e-6f ? glm::normalize(across) : AnyPerpendicular(directions[0]);

						const float vScale = 1.0f / settings.textureSize;
						float v = 0.0f;

						uint32_t lastRing = 0;
						for (int c = 0; c < cylinders; c++) {
							const int previousBone = c == 0 ? parentBone : bones[c - 1];
							const int nextBone = c + 1 < cylinders ? bones[c + 1] : -1;

							const uint32_t firstVertex = static_cast<uint32_t>(mesh.vertices.size());
							for (int k = 0; k <= rings; k++) {
								const float t = static_cast<float>(k) / static_cast<float>(rings);
								const glm::vec3 center = starts[c] + directions[c] * (cylinderLength * t);
								const glm::vec3 axis = k == 0 ? jointDirection(c) : k == rings ? jointDirection(c + 1) : directions[c];

								across = glm::normalize(across - axis * glm::dot(across, axis));
								const glm::vec3 side = glm::cross(axis, across);

								const float along = (static_cast<float>(c) + t) / static_cast<float>(cylinders);
								const float radius = std::max(glm::mix(radiusBase, radiusTip, along), MIN_RADIUS);

								// Halfway along a cylinder only its own bone moves it; towards each end it blends into
								// the neighbouring cylinder's bone, reaching 50/50 at the shared ring
								SkinWeights weights;
								weights.joints[0] = static_cast<uint32_t>(bones[c]);
								if (t <= 0.5f && previousBone >= 0) {
									weights.weights[0] = 0.5f + t;
									weights.joints[1] = static_cast<uint32_t>(previousBone);
									weights.weights[1] = 0.5f - t;
								}
								else if (t > 0.5f && nextBone >= 0) {
									weights.weights[0] = 1.5f - t;
									weights.joints[1] = static_cast<uint32_t>(nextBone);
									weights.weights[1] = t - 0.5f;
								}

								for (int i = 0; i <= radial; i++) {
									const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(radial);
									const glm::vec3 normal = across * std::cos(angle) + side * std::sin(angle);
									const glm::vec3 tangent = -across * std::sin(angle) + side * std::cos(angle);
									const float u = repeatsAround * static_cast<float>(i) / static_cast<float>(radial);
									const float vCoordinate = v + cylinderLength * t * vScale;

									// Shared rings get the same (u, v) from both cylinders: the relief stays watertight
									float offset = 0.0f;
									if (displaced) {
										offset = (settings.height(u, vCoordinate) - settings.displacementMidlevel) * relief;
									}
									const glm::vec3 point = center + normal * (radius + offset);

									Fox::Graphics::Vulkan::Vertex vertex{};
									vertex.position[0] = point.x; vertex.position[1] = point.y; vertex.position[2] = point.z; vertex.position[3] = 1.0f;
									vertex.normal[0] = normal.x; vertex.normal[1] = normal.y; vertex.normal[2] = normal.z;
									vertex.uv[0] = u;
									vertex.uv[1] = vCoordinate;
									// The bitangent, cross(normal, tangent) * handedness, runs along the branch; which way
									// matches the normal map's green channel
									vertex.tangent[0] = tangent.x; vertex.tangent[1] = tangent.y; vertex.tangent[2] = tangent.z; vertex.tangent[3] = handedness;

									mesh.vertices.push_back(vertex);
									mesh.skin.push_back(weights);
								}
							}

							// Counter-clockwise seen from outside
							for (int k = 0; k < rings; k++) {
								for (int i = 0; i < radial; i++) {
									const uint32_t a = firstVertex + static_cast<uint32_t>(k * (radial + 1) + i);
									const uint32_t b = a + static_cast<uint32_t>(radial + 1);
									mesh.indices.insert(mesh.indices.end(), { a, a + 1, b, a + 1, b + 1, b });
								}
							}

							lastRing = firstVertex + static_cast<uint32_t>(rings * (radial + 1));
							v += cylinderLength * vScale;
						}

						// Close the tip with a short cone
						{
							const glm::vec3 tipDirection = directions[cylinders - 1];
							const glm::vec3 tip = starts[cylinders] + tipDirection * std::max(radiusTip, MIN_RADIUS) * 2.0f;
							Fox::Graphics::Vulkan::Vertex vertex{};
							vertex.position[0] = tip.x; vertex.position[1] = tip.y; vertex.position[2] = tip.z; vertex.position[3] = 1.0f;
							vertex.normal[0] = tipDirection.x; vertex.normal[1] = tipDirection.y; vertex.normal[2] = tipDirection.z;
							vertex.uv[1] = v;

							SkinWeights weights;
							weights.joints[0] = static_cast<uint32_t>(bones[cylinders - 1]);

							const uint32_t tipIndex = static_cast<uint32_t>(mesh.vertices.size());
							mesh.vertices.push_back(vertex);
							mesh.skin.push_back(weights);

							for (int i = 0; i < radial; i++) {
								mesh.indices.insert(mesh.indices.end(), { lastRing + i, lastRing + i + 1, tipIndex });
							}
						}

						// --- Branches growing out of this one ---
						if (level >= settings.branchLevels) {
							return;
						}

						// The trunk branches from its third cylinder up, branches from their second
						const int firstBranching = level == 0 ? 2 : 1;
						const int perCylinder = level < 2 ? 2 : 1;
						float azimuth = Random(0.0f, glm::two_pi<float>());

						for (int c = firstBranching; c < cylinders; c++) {
							const glm::vec3 p0 = AnyPerpendicular(directions[c]);
							const glm::vec3 p1 = glm::cross(directions[c], p0);

							for (int n = 0; n < perCylinder; n++) {
								azimuth += GOLDEN_ANGLE;

								const float t = Random(0.25f, 0.85f);
								const glm::vec3 childStart = starts[c] + directions[c] * (cylinderLength * t);
								const float along = (static_cast<float>(c) + t) / static_cast<float>(cylinders);
								const float parentRadius = glm::mix(radiusBase, radiusTip, along);

								const float childRadius = std::max(parentRadius * (level == 0 ? 0.5f : 0.6f), MIN_RADIUS);
								const float childTip = std::max(childRadius * 0.3f, MIN_RADIUS);
								// Shorter towards the top, like a crown
								const float childLength = length * (level == 0 ? 0.55f : 0.6f) * (1.0f - 0.55f * along) * Random(0.8f, 1.15f);

								const glm::vec3 outward = p0 * std::cos(azimuth) + p1 * std::sin(azimuth);
								const float angle = glm::radians(settings.branchAngle * Random(0.8f, 1.2f));
								const glm::vec3 childDirection = glm::normalize(directions[c] * std::cos(angle) + outward * std::sin(angle));

								Grow(bones[c], childStart, childDirection, childLength, childRadius, childTip, level + 1, directions[c]);
							}
						}
					}

					SkinnedMesh mesh;

				private:
					float Random(float low, float high) {
						return std::uniform_real_distribution<float>(low, high)(random);
					}

					glm::vec3 RandomUnit() {
						const float z = Random(-1.0f, 1.0f);
						const float phi = Random(0.0f, glm::two_pi<float>());
						const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
						return glm::vec3(r * std::cos(phi), z, r * std::sin(phi));
					}

					const TreeSettings& settings;
					const float trunkRadius;
					std::mt19937 random;
				};
			}

			SkinnedMesh GenerateTree(const TreeSettings& settings) {
				TreeBuilder builder(settings);

				// The trunk starts a little below the ground, so it meets the ground without a gap
				const float sunk = 0.3f;
				builder.Grow(-1, glm::vec3(0.0f, -sunk, 0.0f), UP, settings.trunkLength + sunk,
					settings.trunkRadius, settings.trunkRadius * 0.2f, 0, glm::vec3(1.0f, 0.0f, 0.0f));

				return std::move(builder.mesh);
			}
		}
	}
}
