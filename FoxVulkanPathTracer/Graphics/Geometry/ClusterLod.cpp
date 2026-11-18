#include "FoxRenderer.h"
#include "ThirdParty/meshoptimizer/meshoptimizer.h"

#include <chrono>
#include <numeric>

namespace Fox {

	namespace Graphics {

		namespace Geometry {

			namespace {
				using Cluster = ClusterLod::Cluster;
				constexpr float INFINITE_ERROR = std::numeric_limits<float>::max();

				// A cluster while the graph is built: its triangles as mesh vertex indices
				struct Building {
					Cluster cluster;
					std::vector<uint32_t> indices;
					std::vector<uint32_t> cards; // Foliage: the cards it holds (into that level's card list)
				};

				struct Positions {
					const float* data;
					size_t count;
					size_t stride;
				};

				Positions PositionsOf(const SkinnedMesh& mesh) {
					return { mesh.vertices.empty() ? nullptr : mesh.vertices[0].position, mesh.vertices.size(), sizeof(Fox::Graphics::Vulkan::Vertex) };
				}

				glm::vec4 SphereOf(const std::vector<uint32_t>& indices, const Positions& positions) {
					const meshopt_Bounds bounds = meshopt_computeClusterBounds(indices.data(), indices.size(), positions.data, positions.count, positions.stride);
					return glm::vec4(bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius);
				}

				// The sphere around the given spheres
				glm::vec4 Enclosing(const std::vector<glm::vec4>& spheres) {
					const meshopt_Bounds bounds = meshopt_computeSphereBounds(&spheres[0].x, spheres.size(), sizeof(glm::vec4), &spheres[0].w, sizeof(glm::vec4));
					return glm::vec4(bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius);
				}

				// Cuts triangles into clusters shaped for ray tracing
				std::vector<std::vector<uint32_t>> Clusterize(const std::vector<uint32_t>& indices, const Positions& positions, const ClusterLod::Settings& settings) {
					const size_t minTriangles = std::max<size_t>(settings.maxTriangles / 4, 1);
					const size_t bound = meshopt_buildMeshletsBound(indices.size(), settings.maxVertices, minTriangles);
					std::vector<meshopt_Meshlet> meshlets(bound);
					std::vector<unsigned int> meshletVertices(indices.size());
					std::vector<unsigned char> meshletTriangles(indices.size());
					const size_t count = meshopt_buildMeshletsSpatial(meshlets.data(), meshletVertices.data(), meshletTriangles.data(), indices.data(), indices.size(),
						positions.data, positions.count, positions.stride, settings.maxVertices, minTriangles, settings.maxTriangles, 0.5f);

					std::vector<std::vector<uint32_t>> clusters(count);
					for (size_t m = 0; m < count; m++) {
						const meshopt_Meshlet& meshlet = meshlets[m];
						clusters[m].resize(meshlet.triangle_count * 3);
						for (size_t i = 0; i < clusters[m].size(); i++) {
							clusters[m][i] = meshletVertices[meshlet.vertex_offset + meshletTriangles[meshlet.triangle_offset + i]];
						}
					}
					return clusters;
				}

				// --- Surfaces: simplified in groups with their borders locked ---

				void BuildSurface(SkinnedMesh& mesh, uint32_t part, const std::vector<uint32_t>& partIndices, const ClusterLod::Settings& settings,
					std::vector<Building>& out) {
					const Positions positions = PositionsOf(mesh);

					std::vector<size_t> current;
					for (std::vector<uint32_t>& indices : Clusterize(partIndices, positions, settings)) {
						Building building;
						building.cluster.part = part;
						building.cluster.selfBounds = SphereOf(indices, positions);
						building.indices = std::move(indices);
						out.push_back(std::move(building));
						current.push_back(out.size() - 1);
					}

					for (uint32_t level = 0; level + 1 < settings.maxLevels && current.size() > 1; level++) {
						// Neighbouring clusters into groups
						std::vector<unsigned int> allIndices, counts;
						for (size_t c : current) {
							allIndices.insert(allIndices.end(), out[c].indices.begin(), out[c].indices.end());
							counts.push_back(static_cast<unsigned int>(out[c].indices.size()));
						}
						std::vector<unsigned int> groupOf(current.size());
						const size_t groupCount = meshopt_partitionClusters(groupOf.data(), allIndices.data(), allIndices.size(), counts.data(), current.size(),
							positions.data, positions.count, positions.stride, settings.groupSize);
						std::vector<std::vector<size_t>> groups(groupCount);
						for (size_t i = 0; i < current.size(); i++) {
							groups[groupOf[i]].push_back(current[i]);
						}

						std::vector<size_t> next;
						for (const std::vector<size_t>& group : groups) {
							std::vector<uint32_t> merged;
							std::vector<glm::vec4> spheres;
							float childError = 0.0f;
							for (size_t c : group) {
								merged.insert(merged.end(), out[c].indices.begin(), out[c].indices.end());
								spheres.push_back(out[c].cluster.selfBounds);
								childError = std::max(childError, out[c].cluster.selfError);
							}

							// Half the triangles, the border with the rest of the mesh where it is
							const size_t target = (merged.size() / 6) * 3;
							std::vector<uint32_t> simplified(merged.size());
							float error = 0.0f;
							const size_t count = meshopt_simplify(simplified.data(), merged.data(), merged.size(), positions.data, positions.count, positions.stride,
								target, INFINITE_ERROR, meshopt_SimplifyLockBorder | meshopt_SimplifySparse | meshopt_SimplifyErrorAbsolute, &error);
							if (count == 0 || count > merged.size() * 85 / 100) {
								// Hardly simpler: these clusters stay the coarsest of their part of the mesh
								for (size_t c : group) {
									out[c].cluster.parentError = INFINITE_ERROR;
								}
								continue;
							}
							simplified.resize(count);

							const glm::vec4 bounds = Enclosing(spheres);
							const float groupError = std::max(childError, error);
							for (size_t c : group) {
								out[c].cluster.parentBounds = bounds;
								out[c].cluster.parentError = groupError;
							}
							for (std::vector<uint32_t>& indices : Clusterize(simplified, positions, settings)) {
								Building building;
								building.cluster.part = part;
								building.cluster.level = level + 1;
								building.cluster.selfBounds = bounds;
								building.cluster.selfError = groupError;
								building.indices = std::move(indices);
								out.push_back(std::move(building));
								next.push_back(out.size() - 1);
							}
						}
						current = std::move(next);
					}
					for (size_t c : current) {
						out[c].cluster.parentError = INFINITE_ERROR;
					}
				}

				// --- Foliage: fewer, larger cards ---

				struct Card {
					std::vector<uint32_t> indices; // Its triangles (mesh vertex indices)
					glm::vec3 center{ 0.0f };
					float area = 0.0f;
					uint32_t vertexCount = 0;
				};

				uint32_t Morton(glm::vec3 p) {
					auto spread = [](uint32_t v) {
						v = (v | (v << 16)) & 0x030000FF;
						v = (v | (v << 8)) & 0x0300F00F;
						v = (v | (v << 4)) & 0x030C30C3;
						v = (v | (v << 2)) & 0x09249249;
						return v;
					};
					const glm::uvec3 q = glm::uvec3(glm::clamp(p, 0.0f, 1.0f) * 1023.0f);
					return spread(q.x) | (spread(q.y) << 1) | (spread(q.z) << 2);
				}

				// Cards (connected pieces of the leaf triangles), in a space filling order
				std::vector<Card> FindCards(const SkinnedMesh& mesh, const std::vector<uint32_t>& partIndices) {
					std::unordered_map<uint32_t, uint32_t> parent;
					std::function<uint32_t(uint32_t)> find = [&](uint32_t v) {
						auto it = parent.find(v);
						if (it == parent.end()) {
							parent[v] = v;
							return v;
						}
						uint32_t root = v;
						while (parent[root] != root) root = parent[root];
						while (parent[v] != root) { const uint32_t up = parent[v]; parent[v] = root; v = up; }
						return root;
					};
					for (size_t t = 0; t + 2 < partIndices.size(); t += 3) {
						const uint32_t a = find(partIndices[t]);
						parent[find(partIndices[t + 1])] = a;
						parent[find(partIndices[t + 2])] = a;
					}

					std::unordered_map<uint32_t, size_t> cardOf;
					std::vector<Card> cards;
					for (size_t t = 0; t + 2 < partIndices.size(); t += 3) {
						const uint32_t root = find(partIndices[t]);
						auto [it, added] = cardOf.emplace(root, cards.size());
						if (added) {
							cards.emplace_back();
						}
						Card& card = cards[it->second];
						const glm::vec3 a(glm::make_vec3(mesh.vertices[partIndices[t]].position));
						const glm::vec3 b(glm::make_vec3(mesh.vertices[partIndices[t + 1]].position));
						const glm::vec3 c(glm::make_vec3(mesh.vertices[partIndices[t + 2]].position));
						card.indices.insert(card.indices.end(), { partIndices[t], partIndices[t + 1], partIndices[t + 2] });
						card.area += 0.5f * glm::length(glm::cross(b - a, c - a));
					}
					glm::vec3 low(std::numeric_limits<float>::max()), high(-std::numeric_limits<float>::max());
					for (Card& card : cards) {
						std::vector<uint32_t> unique = card.indices;
						std::sort(unique.begin(), unique.end());
						unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
						card.vertexCount = static_cast<uint32_t>(unique.size());
						for (uint32_t v : unique) {
							card.center += glm::vec3(glm::make_vec3(mesh.vertices[v].position));
						}
						card.center /= static_cast<float>(std::max<size_t>(unique.size(), 1));
						low = glm::min(low, card.center);
						high = glm::max(high, card.center);
					}
					const glm::vec3 extent = glm::max(high - low, glm::vec3(1e-6f));
					std::vector<uint32_t> codes(cards.size());
					for (size_t i = 0; i < cards.size(); i++) {
						codes[i] = Morton((cards[i].center - low) / extent);
					}
					std::vector<size_t> order(cards.size());
					std::iota(order.begin(), order.end(), 0);
					std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return codes[a] < codes[b]; });
					std::vector<Card> sorted;
					sorted.reserve(cards.size());
					for (size_t i : order) {
						sorted.push_back(std::move(cards[i]));
					}
					return sorted;
				}

				// Whole cards, in order, into clusters
				std::vector<std::vector<uint32_t>> PackCards(const std::vector<Card>& cards, const std::vector<uint32_t>& which, const ClusterLod::Settings& settings) {
					std::vector<std::vector<uint32_t>> clusters;
					uint32_t triangles = 0, vertices = 0;
					for (uint32_t card : which) {
						const uint32_t cardTriangles = static_cast<uint32_t>(cards[card].indices.size() / 3);
						if (clusters.empty() || triangles + cardTriangles > settings.maxTriangles || vertices + cards[card].vertexCount > settings.maxVertices) {
							clusters.emplace_back();
							triangles = vertices = 0;
						}
						clusters.back().push_back(card);
						triangles += cardTriangles;
						vertices += cards[card].vertexCount;
					}
					return clusters;
				}

				void BuildFoliage(SkinnedMesh& mesh, uint32_t part, const std::vector<uint32_t>& partIndices, const ClusterLod::Settings& settings,
					std::vector<Building>& out, uint32_t& addedVertices) {
					std::vector<Card> cards = FindCards(mesh, partIndices);
					if (cards.empty()) {
						return;
					}

					auto addCluster = [&](const std::vector<uint32_t>& members, uint32_t level, const glm::vec4& bounds, float error) {
						Building building;
						building.cluster.part = part;
						building.cluster.level = level;
						building.cluster.foliage = true;
						for (uint32_t card : members) {
							building.indices.insert(building.indices.end(), cards[card].indices.begin(), cards[card].indices.end());
						}
						building.cluster.selfBounds = level == 0 ? SphereOf(building.indices, PositionsOf(mesh)) : bounds;
						building.cluster.selfError = error;
						building.cards = members;
						out.push_back(std::move(building));
						return out.size() - 1;
					};

					std::vector<uint32_t> all(cards.size());
					std::iota(all.begin(), all.end(), 0u);
					std::vector<size_t> current;
					for (const std::vector<uint32_t>& members : PackCards(cards, all, settings)) {
						current.push_back(addCluster(members, 0, glm::vec4(0.0f), 0.0f));
					}

					for (uint32_t level = 0; level + 1 < settings.maxLevels && current.size() > 1; level++) {
						std::vector<size_t> next;
						// Consecutive clusters (the cards are in space filling order) make a group
						for (size_t first = 0; first < current.size(); first += settings.groupSize) {
							const size_t last = std::min(first + settings.groupSize, current.size());
							std::vector<uint32_t> members;
							std::vector<glm::vec4> spheres;
							float childError = 0.0f;
							for (size_t i = first; i < last; i++) {
								const Building& child = out[current[i]];
								members.insert(members.end(), child.cards.begin(), child.cards.end());
								spheres.push_back(child.cluster.selfBounds);
								childError = std::max(childError, child.cluster.selfError);
							}
							if (members.size() < 2) {
								for (size_t i = first; i < last; i++) {
									out[current[i]].cluster.parentError = INFINITE_ERROR;
								}
								continue;
							}

							// Every other card, grown to cover what the dropped ones did
							float totalArea = 0.0f, keptArea = 0.0f;
							for (size_t i = 0; i < members.size(); i++) {
								totalArea += cards[members[i]].area;
								keptArea += i % 2 == 0 ? cards[members[i]].area : 0.0f;
							}
							const float scale = std::sqrt(totalArea / std::max(keptArea, 1e-12f));

							std::vector<uint32_t> grown;
							float grownArea = 0.0f;
							for (size_t i = 0; i < members.size(); i += 2) {
								// A copy of the card's vertices, moved out from its centre (same texture coordinates and skin)
								Card card;
								const Card& source = cards[members[i]];
								std::unordered_map<uint32_t, uint32_t> copyOf;
								for (uint32_t v : source.indices) {
									auto [it, added] = copyOf.emplace(v, static_cast<uint32_t>(mesh.vertices.size()));
									if (added) {
										Fox::Graphics::Vulkan::Vertex vertex = mesh.vertices[v];
										const glm::vec3 p = source.center + (glm::vec3(glm::make_vec3(vertex.position)) - source.center) * scale;
										vertex.position[0] = p.x;
										vertex.position[1] = p.y;
										vertex.position[2] = p.z;
										mesh.vertices.push_back(vertex);
										mesh.skin.push_back(v < mesh.skin.size() ? mesh.skin[v] : SkinWeights{});
										addedVertices++;
									}
									card.indices.push_back(it->second);
								}
								card.center = source.center;
								card.area = source.area * scale * scale;
								card.vertexCount = source.vertexCount;
								grownArea += card.area;
								grown.push_back(static_cast<uint32_t>(cards.size()));
								cards.push_back(std::move(card));
							}

							// The error: about half the size of the grown cards
							const float error = std::max(childError, 0.5f * std::sqrt(grownArea / static_cast<float>(grown.size())));
							const glm::vec4 bounds = Enclosing(spheres);
							for (size_t i = first; i < last; i++) {
								out[current[i]].cluster.parentBounds = bounds;
								out[current[i]].cluster.parentError = error;
							}
							for (const std::vector<uint32_t>& packed : PackCards(cards, grown, settings)) {
								next.push_back(addCluster(packed, level + 1, bounds, error));
							}
						}
						if (next.empty()) {
							break;
						}
						current = std::move(next);
					}
					for (size_t c : current) {
						out[c].cluster.parentError = INFINITE_ERROR;
					}
				}
			}

			ClusterLod BuildClusterLod(SkinnedMesh& mesh, const std::vector<bool>& foliage, const ClusterLod::Settings& settings) {
				const auto start = std::chrono::steady_clock::now();
				ClusterLod lod;
				if (mesh.skin.size() < mesh.vertices.size()) {
					mesh.skin.resize(mesh.vertices.size()); // Every vertex has weights (appended vertices copy them)
				}

				std::vector<SkinnedMesh::Part> parts = mesh.parts;
				if (parts.empty()) {
					parts.push_back({ 0, static_cast<uint32_t>(mesh.indices.size()), 0 });
				}

				std::vector<Building> built;
				for (uint32_t p = 0; p < parts.size(); p++) {
					const std::vector<uint32_t> partIndices(mesh.indices.begin() + parts[p].firstIndex, mesh.indices.begin() + parts[p].firstIndex + parts[p].indexCount);
					if (partIndices.empty()) {
						continue;
					}
					if (p < foliage.size() && foliage[p]) {
						BuildFoliage(mesh, p, partIndices, settings, built, lod.addedVertices);
					}
					else {
						BuildSurface(mesh, p, partIndices, settings, built);
					}
				}

				// Level order, then the arrays the GPU takes
				// (an order of indices: stable_sort cannot buffer the 16 byte aligned clusters)
				std::vector<size_t> order(built.size());
				std::iota(order.begin(), order.end(), 0);
				std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return built[a].cluster.level < built[b].cluster.level; });
				for (size_t o : order) {
					Building& building = built[o];
					Cluster cluster = building.cluster;
					cluster.firstIndex = static_cast<uint32_t>(lod.indices.size());
					cluster.triangleCount = static_cast<uint32_t>(building.indices.size() / 3);
					cluster.firstVertex = static_cast<uint32_t>(lod.localVertices.size());

					unsigned int vertices[256];
					std::vector<unsigned char> local(building.indices.size());
					cluster.vertexCount = static_cast<uint32_t>(meshopt_extractMeshletIndices(vertices, local.data(), building.indices.data(), building.indices.size()));
					lod.localVertices.insert(lod.localVertices.end(), vertices, vertices + cluster.vertexCount);
					lod.localIndices.insert(lod.localIndices.end(), local.begin(), local.end());
					lod.indices.insert(lod.indices.end(), building.indices.begin(), building.indices.end());

					if (lod.levels.size() <= cluster.level) {
						lod.levels.resize(cluster.level + 1, { 0, 0, INFINITE_ERROR, 0.0f });
						lod.levels[cluster.level].firstCluster = static_cast<uint32_t>(lod.clusters.size());
					}
					ClusterLod::Level& level = lod.levels[cluster.level];
					level.clusterCount++;
					level.minSelfError = std::min(level.minSelfError, cluster.selfError);
					level.maxParentError = std::max(level.maxParentError, cluster.parentError);
					lod.clusters.push_back(cluster);
				}

				lod.buildSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
				return lod;
			}
		}
	}
}
