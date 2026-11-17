#pragma once

#include <cstdint>

namespace Fox {

	namespace Graphics {

		namespace Geometry {

			// Grows a tree trunk with branches as one skinned mesh. Every branch is a chain of tapered cylinders;
			// each cylinder gets a bone as it is built, parented to the previous cylinder of the branch, and a
			// branch's first bone to the cylinder it grows out of. Rings where two cylinders meet are shared
			// between their bones, so the tree bends smoothly at its joints.
			struct TreeSettings {
				uint32_t seed = 11;
				float trunkLength = 7.0f;   // Meters
				float trunkRadius = 0.3f;   // At the base
				int branchLevels = 3;       // Levels of branches growing out of the trunk
				float branchAngle = 50.0f;  // Degrees between a branch and its parent, on average
				float bendiness = 0.09f;    // Random change of direction from one cylinder to the next
				float upwardTendency = 0.14f; // Branches turn towards the light
				int ringsPerCylinder = 3;   // Subdivisions along each cylinder, so bends stay round

				// --- Bark texture mapping ---
				float textureSize = 1.0f;   // Meters of bark one repeat of the textures covers (rounded so the
				                            // texture repeats a whole number of times around each branch: no seam)
				bool normalMapGreenUp = true; // OpenGL convention (green = up the image); false: DirectX

				// --- Displacement: real relief, pushed out along the normal ---
				// Height in [0, 1] at texture coordinates (u, v). Empty: smooth cylinders.
				std::function<float(float u, float v)> height;
				float displacementScale = 0.15f;   // Meters between heights 0 and 1, on the trunk
				float displacementMidlevel = 0.5f; // Height that stays on the cylinder
				float detailEdgeLength = 0.03f;    // Triangle size on displaced branches, meters
				float minDisplacedRadius = 0.025f; // Thinner branches stay smooth and coarse
			};

			SkinnedMesh GenerateTree(const TreeSettings& settings);
		}
	}
}
