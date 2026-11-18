#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace Fox {

	namespace Graphics {

		namespace Geometry {

			// Continuous level of detail from clusters, after Nanite.
			//
			// The mesh is cut into clusters of up to ~128 triangles. Neighbouring clusters are grouped, each group is
			// simplified to about half its triangles with its border kept where it is, and the result is cut into new
			// clusters: the next level. Repeating this builds a graph (a DAG) from the full mesh up to a handful of
			// clusters. Every group records the error its simplification made (in mesh units) and a bounding sphere.
			//
			// Each frame, for each placed instance, a cluster is used when its own error looks small enough from the
			// camera but its group's parent error does not:
			//
			//     projected(selfError, selfBounds) <= threshold  &&  projected(parentError, parentBounds) > threshold
			//
			// The clusters of a group share their parent error and bounds, and errors only grow up the graph, so the
			// clusters chosen always cover the mesh once, without cracks: group borders never move.
			//
			// Surfaces (bark) are simplified with meshoptimizer. Foliage (leaf cards with an alpha cutout) cannot be
			// simplified that way without wrecking the cutouts, so its levels thin the cards instead: each group
			// keeps half of its cards and enlarges them so the canopy covers as much. Enlarged cards are new vertices
			// (appended to the mesh, with the skin weights of the card they come from) with the same texture
			// coordinates, so their opacity micromaps are those of the original cards.
			//
			// All levels index the mesh's vertices, so skinning the mesh (wind) moves every level.
			struct ClusterLod {
				struct Settings {
					uint32_t maxTriangles = 128;
					uint32_t maxVertices = 128;
					uint32_t groupSize = 8;      // Clusters simplified together
					uint32_t maxLevels = 16;
				};

				struct Cluster {
					uint32_t part = 0;           // The mesh part (material) it belongs to
					uint32_t level = 0;          // 0: full detail
					bool foliage = false;

					uint32_t firstIndex = 0;     // Into indices and localIndices: 3 per triangle
					uint32_t triangleCount = 0;
					uint32_t firstVertex = 0;    // Into localVertices: its vertices, in local order
					uint32_t vertexCount = 0;

					glm::vec4 selfBounds{ 0.0f };   // Centre, radius (mesh space)
					float selfError = 0.0f;         // Mesh units; 0 for full detail
					glm::vec4 parentBounds{ 0.0f };
					float parentError = 0.0f;       // Of the group that replaces it; infinite for the coarsest
				};

				// The clusters of each level, in level order
				struct Level {
					uint32_t firstCluster = 0;
					uint32_t clusterCount = 0;
					float minSelfError = 0.0f;
					float maxParentError = 0.0f;
				};

				std::vector<Cluster> clusters;       // Level 0 first, then level 1, ...
				std::vector<Level> levels;
				std::vector<uint32_t> indices;       // Mesh vertex indices, 3 per triangle
				std::vector<uint8_t> localIndices;   // The same triangles as indices into the cluster's vertices
				std::vector<uint32_t> localVertices; // Mesh vertex indices, per cluster
				uint32_t addedVertices = 0;          // Appended to the mesh (enlarged leaf cards)
				float buildSeconds = 0.0f;

				// The most clusters any cut through the graph can have: the full detail level's count
				uint32_t MaxCutClusters() const { return levels.empty() ? 0 : levels[0].clusterCount; }

				size_t TriangleCount() const { return indices.size() / 3; }
			};

			// Builds the graph for a skinned mesh (bind pose). foliage: per part of the mesh, whether it is leaf cards.
			// Enlarged leaf cards are appended to mesh.vertices and mesh.skin; the rest of the mesh is unchanged.
			ClusterLod BuildClusterLod(SkinnedMesh& mesh, const std::vector<bool>& foliage, const ClusterLod::Settings& settings = {});
		}
	}
}
