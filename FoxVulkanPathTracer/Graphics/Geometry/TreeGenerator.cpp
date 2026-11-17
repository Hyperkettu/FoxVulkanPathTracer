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
				const int SHAPE_SAMPLES = 256;          // Dense samples of a cross-section, for even spacing

				glm::vec3 AnyPerpendicular(const glm::vec3& direction) {
					const glm::vec3 other = std::abs(direction.y) < 0.9f ? UP : glm::vec3(1.0f, 0.0f, 0.0f);
					return glm::normalize(glm::cross(direction, other));
				}

				// The outline of one ring, unit scale: radial + 1 points (the last repeats the first, for the
				// texture seam), spaced evenly along the outline, with outward normals and tangents
				struct RingShape {
					std::vector<glm::vec2> points;
					std::vector<glm::vec2> normals;
					std::vector<glm::vec2> tangents; // Direction of increasing u
				};

				// strength 0: a circle; 1: the cross-section spline; in between: blended
				RingShape MakeRing(const ClosedSpline& spline, float strength, int radial) {
					RingShape ring;
					ring.points.resize(radial + 1);
					ring.normals.resize(radial + 1);
					ring.tangents.resize(radial + 1);

					if (strength <= 0.0f) {
						for (int i = 0; i <= radial; i++) {
							const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(radial);
							ring.points[i] = glm::vec2(std::cos(angle), std::sin(angle));
							ring.normals[i] = ring.points[i];
							ring.tangents[i] = glm::vec2(-std::sin(angle), std::cos(angle));
						}
						return ring;
					}

					// Densely sampled outline, and the length along it
					std::vector<glm::vec2> dense(SHAPE_SAMPLES + 1);
					std::vector<float> lengthTo(SHAPE_SAMPLES + 1, 0.0f);
					float area = 0.0f;
					for (int k = 0; k <= SHAPE_SAMPLES; k++) {
						const float t = static_cast<float>(k) / static_cast<float>(SHAPE_SAMPLES);
						const float angle = glm::two_pi<float>() * t;
						dense[k] = glm::mix(glm::vec2(std::cos(angle), std::sin(angle)), spline.Evaluate(t), strength);
						if (k > 0) {
							lengthTo[k] = lengthTo[k - 1] + glm::length(dense[k] - dense[k - 1]);
							area += dense[k - 1].x * dense[k].y - dense[k].x * dense[k - 1].y;
						}
					}
					const float total = std::max(lengthTo[SHAPE_SAMPLES], 1e-6f);
					// Clockwise outlines get their normals flipped, so they always point out
					const float outward = area >= 0.0f ? 1.0f : -1.0f;

					int k = 0;
					for (int i = 0; i <= radial; i++) {
						const float target = total * static_cast<float>(i) / static_cast<float>(radial);
						while (k < SHAPE_SAMPLES - 1 && lengthTo[k + 1] < target) {
							k++;
						}
						const float span = std::max(lengthTo[k + 1] - lengthTo[k], 1e-9f);
						const float f = glm::clamp((target - lengthTo[k]) / span, 0.0f, 1.0f);

						ring.points[i] = glm::mix(dense[k], dense[k + 1], f);
						glm::vec2 tangent = dense[k + 1] - dense[k];
						tangent = glm::dot(tangent, tangent) > 1e-12f ? glm::normalize(tangent) : glm::vec2(0.0f, 1.0f);
						ring.tangents[i] = tangent * outward;
						ring.normals[i] = glm::vec2(tangent.y, -tangent.x) * outward;
					}
					ring.points[radial] = ring.points[0];
					ring.normals[radial] = ring.normals[0];
					ring.tangents[radial] = ring.tangents[0];
					return ring;
				}

				// Where a branch runs: one entry per cylinder
				struct BranchPath {
					std::vector<glm::vec3> starts;     // cylinders + 1: the last is the end of the branch
					std::vector<glm::vec3> directions;
					std::vector<float> lengths;
					float total = 0.0f;
				};

				class TreeBuilder {
				public:
					TreeBuilder(const TreeSettings& settings)
						: settings(settings), trunkRadius(std::max(settings.trunkRadius, 1e-3f)),
						random(settings.seed), leafRandom(settings.seed * 7919u + 1u) {}

					// The trunk: its centre line follows the curve spline, with a little wandering on top
					BranchPath TrunkPath(glm::vec3 base, float length) {
						OpenSpline<glm::vec2> curve;
						curve.points = settings.trunkCurve;

						const int cylinders = std::max(settings.trunkCylinders, 1);
						std::vector<glm::vec3> points(cylinders + 1);
						glm::vec3 wander(0.0f);
						for (int c = 0; c <= cylinders; c++) {
							const float t = static_cast<float>(c) / static_cast<float>(cylinders);
							const glm::vec2 offset = curve.points.empty() ? glm::vec2(0.0f) : curve.Evaluate(t);
							if (c > 0) {
								glm::vec3 step = RandomUnit();
								step.y = 0.0f;
								wander += step * (settings.trunkWander * length / static_cast<float>(cylinders));
							}
							points[c] = base + UP * (length * t) + glm::vec3(offset.x, 0.0f, offset.y) + wander;
						}
						return PathThrough(points);
					}

					// A branch: straight segments that wander and turn upwards
					BranchPath BranchPathFrom(glm::vec3 start, glm::vec3 direction, float length, int level) {
						const int cylinders = CYLINDERS[std::min(level, 3)];
						std::vector<glm::vec3> points(cylinders + 1);
						points[0] = start;
						glm::vec3 heading = glm::normalize(direction);
						for (int c = 0; c < cylinders; c++) {
							if (c > 0) {
								heading = glm::normalize(heading + RandomUnit() * settings.bendiness + UP * settings.upwardTendency);
							}
							points[c + 1] = points[c] + heading * (length / static_cast<float>(cylinders));
						}
						return PathThrough(points);
					}

					// One branch (the trunk is the first): a chain of cylinders, then leaves and the branches growing out
					// of it. reference: a direction across the parent, so rings line up with it (no twist at the base).
					void Grow(int parentBone, const BranchPath& path, float radiusBase, float radiusTip, int level, glm::vec3 reference) {
						const int cylinders = static_cast<int>(path.directions.size());
						const auto& starts = path.starts;
						const auto& directions = path.directions;
						const auto& lengths = path.lengths;

						// Displaced branches are subdivided finely enough to carry the relief
						const bool displaced = static_cast<bool>(settings.height) && radiusBase >= settings.minDisplacedRadius;
						const float circumference = glm::two_pi<float>() * radiusBase;
						const int radial = displaced
							? std::clamp(static_cast<int>(std::round(circumference / settings.detailEdgeLength)), RADIAL[std::min(level, 3)], 160)
							: RADIAL[std::min(level, 3)];

						// Texture repeats around the branch: a whole number, so the seam matches. Relief is scaled down
						// on thinner branches (bark texture is made for a trunk).
						const float repeatsAround = std::max(1.0f, std::round(circumference / settings.textureSize));
						const float relief = settings.displacementScale * std::min(1.0f, radiusBase / trunkRadius);
						const float handedness = settings.normalMapGreenUp ? -1.0f : 1.0f;

						// --- Rig: one bone per cylinder ---
						std::vector<int> bones(cylinders);
						for (int c = 0; c < cylinders; c++) {
							Bone bone;
							bone.parent = c == 0 ? parentBone : bones[c - 1];
							bone.origin = starts[c];
							bone.rotation = glm::rotation(UP, directions[c]);
							bone.length = lengths[c];
							bone.radius = glm::mix(radiusBase, radiusTip, (static_cast<float>(c) + 0.5f) / static_cast<float>(cylinders));

							bones[c] = static_cast<int>(mesh.bones.size());
							mesh.bones.push_back(bone);
						}

						// --- Geometry: the cylinders, batched into the mesh ---

						// Where two cylinders meet, the shared ring faces halfway between them
						auto jointDirection = [&](int joint) {
							if (joint == 0) return directions[0];
							if (joint == cylinders) return directions[cylinders - 1];
							return glm::normalize(directions[joint - 1] + directions[joint]);
						};

						// How much of the cross-section shape a ring shows (0: round)
						auto shapeStrength = [&](float along) {
							const float trunkShape = glm::mix(settings.crossSectionBase, settings.crossSectionTop, along);
							return level == 0 ? trunkShape : settings.branchCrossSection * settings.crossSectionTop;
						};

						// Ring orientation, carried along the branch without twisting
						glm::vec3 across = reference - directions[0] * glm::dot(reference, directions[0]);
						across = glm::dot(across, across) > 1e-6f ? glm::normalize(across) : AnyPerpendicular(directions[0]);

						const float vScale = 1.0f / settings.textureSize;
						float v = 0.0f;
						float lengthBefore = 0.0f;

						uint32_t lastRing = 0;
						for (int c = 0; c < cylinders; c++) {
							const int previousBone = c == 0 ? parentBone : bones[c - 1];
							const int nextBone = c + 1 < cylinders ? bones[c + 1] : -1;
							const int rings = displaced
								? std::max(settings.ringsPerCylinder, static_cast<int>(std::ceil(lengths[c] / settings.detailEdgeLength)))
								: std::max(settings.ringsPerCylinder, 1);

							const uint32_t firstVertex = static_cast<uint32_t>(mesh.vertices.size());
							for (int k = 0; k <= rings; k++) {
								const float t = static_cast<float>(k) / static_cast<float>(rings);
								const glm::vec3 center = starts[c] + directions[c] * (lengths[c] * t);
								const glm::vec3 axis = k == 0 ? jointDirection(c) : k == rings ? jointDirection(c + 1) : directions[c];

								across = glm::normalize(across - axis * glm::dot(across, axis));
								const glm::vec3 side = glm::cross(axis, across);

								// Taper by length along the branch (the same for shared rings of both cylinders)
								const float along = std::clamp((lengthBefore + lengths[c] * t) / std::max(path.total, 1e-6f), 0.0f, 1.0f);
								const float radius = std::max(glm::mix(radiusBase, radiusTip, along), MIN_RADIUS);
								const RingShape shape = MakeRing(settings.crossSection, shapeStrength(along), radial);

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
									const glm::vec3 offsetDirection = across * shape.points[i].x + side * shape.points[i].y;
									const glm::vec3 normal = glm::normalize(across * shape.normals[i].x + side * shape.normals[i].y);
									const glm::vec3 tangent = glm::normalize(across * shape.tangents[i].x + side * shape.tangents[i].y);
									const float u = repeatsAround * static_cast<float>(i) / static_cast<float>(radial);
									const float vCoordinate = v + lengths[c] * t * vScale;

									// Shared rings get the same (u, v) from both cylinders: the relief stays watertight
									float offset = 0.0f;
									if (displaced) {
										offset = (settings.height(u, vCoordinate) - settings.displacementMidlevel) * relief;
									}
									const glm::vec3 point = center + offsetDirection * radius + normal * offset;

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
							v += lengths[c] * vScale;
							lengthBefore += lengths[c];
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

						// --- Leaves: cards along the branch, and one at its tip ---
						if (settings.leaves && level >= settings.leafLevel) {
							for (int c = 0; c < cylinders; c++) {
								const glm::vec3 p0 = AnyPerpendicular(directions[c]);
								const glm::vec3 p1 = glm::cross(directions[c], p0);

								for (int n = 0; n < settings.leavesPerCylinder; n++) {
									const float t = LeafRandom(0.1f, 0.95f);
									const float along = (static_cast<float>(c) + t) / static_cast<float>(cylinders);
									const float radius = glm::mix(radiusBase, radiusTip, along);
									const float azimuth = LeafRandom(0.0f, glm::two_pi<float>());
									const glm::vec3 outward = p0 * std::cos(azimuth) + p1 * std::sin(azimuth);

									// Out from the branch, forward along it, a little upwards
									const glm::vec3 leafDirection = glm::normalize(outward * 0.75f + directions[c] * 0.55f + UP * 0.25f);
									AddLeafCard(bones[c], starts[c] + directions[c] * (lengths[c] * t) + outward * (radius * 0.8f), leafDirection);
								}
							}
							AddLeafCard(bones[cylinders - 1], starts[cylinders], glm::normalize(directions[cylinders - 1] + UP * 0.2f));
						}

						// --- Branches growing out of this one ---
						if (level >= settings.branchLevels) {
							return;
						}

						const int firstBranching = level == 0
							? std::clamp(static_cast<int>(std::floor(settings.firstBranchHeight * static_cast<float>(cylinders))), 0, cylinders - 1)
							: 1;
						const int perCylinder = level == 0 ? settings.trunkBranches
							: level == 1 ? settings.branchBranches
							: std::max(1, settings.branchBranches / 2);
						float azimuth = Random(0.0f, glm::two_pi<float>());
						float lengthBefore2 = 0.0f;
						for (int c = 0; c < firstBranching; c++) {
							lengthBefore2 += lengths[c];
						}

						for (int c = firstBranching; c < cylinders; c++) {
							const glm::vec3 p0 = AnyPerpendicular(directions[c]);
							const glm::vec3 p1 = glm::cross(directions[c], p0);

							for (int n = 0; n < perCylinder; n++) {
								azimuth += GOLDEN_ANGLE + settings.azimuthVariation * Random(-1.0f, 1.0f);

								// Evenly spread along the cylinder, shifted at random
								const float even = (static_cast<float>(n) + 0.5f) / static_cast<float>(perCylinder);
								const float t = glm::clamp(glm::mix(even, Random(0.05f, 0.95f), settings.positionVariation), 0.02f, 0.98f);
								const glm::vec3 childStart = starts[c] + directions[c] * (lengths[c] * t);
								const float along = (lengthBefore2 + lengths[c] * t) / std::max(path.total, 1e-6f);
								const float parentRadius = glm::mix(radiusBase, radiusTip, along);

								const float childRadius = std::max(parentRadius * settings.branchRadiusRatio, MIN_RADIUS);
								const float childTip = std::max(childRadius * 0.3f, MIN_RADIUS);
								// Shorter towards the top, like a crown
								const float childLength = path.total * settings.branchLengthRatio * (1.0f - 0.55f * along)
									* std::max(1.0f + settings.lengthVariation * Random(-1.0f, 1.0f), 0.1f);

								const glm::vec3 outward = p0 * std::cos(azimuth) + p1 * std::sin(azimuth);
								const float angle = glm::radians(settings.branchAngle * (1.0f + settings.angleVariation * Random(-1.0f, 1.0f)));
								const glm::vec3 childDirection = glm::normalize(directions[c] * std::cos(angle) + outward * std::sin(angle));

								Grow(bones[c], BranchPathFrom(childStart, childDirection, childLength, level + 1), childRadius, childTip, level + 1, directions[c]);
							}
							lengthBefore2 += lengths[c];
						}
					}

					SkinnedMesh mesh;
					std::vector<uint32_t> leafIndices; // Second part of the mesh

				private:
					static BranchPath PathThrough(const std::vector<glm::vec3>& points) {
						BranchPath path;
						path.starts = points;
						for (size_t c = 0; c + 1 < points.size(); c++) {
							const glm::vec3 segment = points[c + 1] - points[c];
							const float length = std::max(glm::length(segment), 1e-4f);
							path.directions.push_back(segment / length);
							path.lengths.push_back(length);
							path.total += length;
						}
						return path;
					}

					// A card of the leaf texture: the texture's bottom edge (its stem) at base, its top towards
					// direction. Subdivided, drooping towards its tip and displaced; skinned to one bone.
					void AddLeafCard(int bone, glm::vec3 base, glm::vec3 direction) {
						const float length = settings.leafSize * std::max(1.0f + settings.leafSizeVariation * LeafRandom(-1.0f, 1.0f), 0.1f);
						const float width = length * settings.leafAspect;

						// Face upwards, rolled randomly about the card's length
						glm::vec3 side = glm::cross(direction, UP);
						side = glm::dot(side, side) > 0.01f ? glm::normalize(side) : AnyPerpendicular(direction);
						glm::vec3 normal = glm::cross(side, direction);
						if (normal.y < 0.0f) {
							side = -side;
							normal = -normal;
						}
						const float roll = glm::radians(settings.leafRollVariation * LeafRandom(-1.0f, 1.0f));
						const glm::vec3 rolledSide = side * std::cos(roll) + normal * std::sin(roll);
						normal = glm::normalize(glm::cross(rolledSide, direction));
						side = rolledSide;

						// The bitangent, cross(normal, tangent) * handedness, must run up the texture for a normal map
						// with green up; cross(normal, side) is the card's direction, which is up the texture
						const float handedness = settings.leafNormalMapGreenUp ? 1.0f : -1.0f;

						SkinWeights weights;
						weights.joints[0] = static_cast<uint32_t>(bone);

						const int columns = std::max(settings.leafColumns, 1);
						const int rows = std::max(settings.leafRows, 1);
						const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());

						for (int r = 0; r <= rows; r++) {
							const float t = static_cast<float>(r) / static_cast<float>(rows);
							// Droop: down by curl * t^2, so the surface tilts along its length
							const glm::vec3 alongCard = direction * length - UP * (2.0f * settings.leafCurl * t);
							glm::vec3 rowNormal = glm::normalize(glm::cross(side, alongCard));
							if (glm::dot(rowNormal, normal) < 0.0f) {
								rowNormal = -rowNormal;
							}

							for (int col = 0; col <= columns; col++) {
								const float s = static_cast<float>(col) / static_cast<float>(columns);
								const float u = s;
								const float v = 1.0f - t; // The stem is at the bottom of the texture

								float relief = 0.0f;
								if (settings.leafHeight) {
									relief = (settings.leafHeight(u, v) - settings.leafDisplacementMidlevel) * settings.leafDisplacementScale;
								}

								const glm::vec3 point = base + direction * (length * t) + side * (width * (s - 0.5f))
									- UP * (settings.leafCurl * t * t) + rowNormal * relief;

								Fox::Graphics::Vulkan::Vertex vertex{};
								vertex.position[0] = point.x; vertex.position[1] = point.y; vertex.position[2] = point.z; vertex.position[3] = 1.0f;
								vertex.normal[0] = rowNormal.x; vertex.normal[1] = rowNormal.y; vertex.normal[2] = rowNormal.z;
								vertex.uv[0] = u;
								vertex.uv[1] = v;
								vertex.tangent[0] = side.x; vertex.tangent[1] = side.y; vertex.tangent[2] = side.z; vertex.tangent[3] = handedness;

								mesh.vertices.push_back(vertex);
								mesh.skin.push_back(weights);
							}
						}

						// Every card has the same layout, so its triangles share opacity micromap entries
						for (int r = 0; r < rows; r++) {
							for (int col = 0; col < columns; col++) {
								const uint32_t a = first + static_cast<uint32_t>(r * (columns + 1) + col);
								const uint32_t b = a + static_cast<uint32_t>(columns + 1);
								leafIndices.insert(leafIndices.end(), { a, a + 1, b, a + 1, b + 1, b });
							}
						}
					}

					float Random(float low, float high) {
						return std::uniform_real_distribution<float>(low, high)(random);
					}

					// Leaves draw from their own random sequence, so they do not change the branches
					float LeafRandom(float low, float high) {
						return std::uniform_real_distribution<float>(low, high)(leafRandom);
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
					std::mt19937 leafRandom;
				};
			}

			SkinnedMesh GenerateTree(const TreeSettings& settings) {
				TreeBuilder builder(settings);

				// The trunk starts a little below the ground, so it meets the ground without a gap
				const float sunk = 0.3f;
				const BranchPath trunk = builder.TrunkPath(glm::vec3(0.0f, -sunk, 0.0f), settings.trunkLength + sunk);
				builder.Grow(-1, trunk, settings.trunkRadius, settings.trunkRadius * settings.trunkTaper, 0, glm::vec3(1.0f, 0.0f, 0.0f));

				// Bark first, then the leaves: two parts with their own materials
				SkinnedMesh mesh = std::move(builder.mesh);
				const uint32_t barkIndices = static_cast<uint32_t>(mesh.indices.size());
				mesh.parts.push_back({ 0, barkIndices, 0 });
				if (!builder.leafIndices.empty()) {
					mesh.indices.insert(mesh.indices.end(), builder.leafIndices.begin(), builder.leafIndices.end());
					mesh.parts.push_back({ barkIndices, static_cast<uint32_t>(builder.leafIndices.size()), 1 });
				}
				return mesh;
			}
		}
	}
}
