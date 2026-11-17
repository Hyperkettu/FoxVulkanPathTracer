#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace Fox {

	namespace Graphics {

		namespace Geometry {

			// Grows a tree trunk with branches as one skinned mesh. Every branch is a chain of tapered cylinders;
			// each cylinder gets a bone as it is built, parented to the previous cylinder of the branch, and a
			// branch's first bone to the cylinder it grows out of. Rings where two cylinders meet are shared
			// between their bones, so the tree bends smoothly at its joints.
			//
			// The trunk's rings take their shape from a closed spline (cross-section) and its centre line follows
			// an open spline from the base to the top (curve). All randomness comes from the seed, scaled by the
			// variation settings: the same settings always grow the same tree.
			struct TreeSettings {
				// --- Trunk ---
				float trunkLength = 7.0f;   // Meters
				float trunkRadius = 0.3f;   // At the base
				float trunkTaper = 0.2f;    // Radius at the top, relative to the base
				int trunkCylinders = 8;     // Segments (and bones) along the trunk

				// Cross-section: a closed spline, 1 = the trunk radius. A circle gives a round trunk.
				ClosedSpline crossSection = ClosedSpline::Circle(8);
				float crossSectionBase = 1.0f; // How much of the shape shows at the base (0 = round)...
				float crossSectionTop = 0.4f;  // ...and at the top; it blends in between
				float branchCrossSection = 0.0f; // How much of the shape branches take on (0 = round)

				// Centre line: horizontal offsets (x, z) in meters at evenly spaced heights, base to top. All zero:
				// straight up. A spline runs through them.
				std::vector<glm::vec2> trunkCurve = { glm::vec2(0.0f), glm::vec2(0.0f), glm::vec2(0.0f), glm::vec2(0.0f), glm::vec2(0.0f) };

				// --- Branches ---
				int branchLevels = 3;          // Levels of branches growing out of the trunk
				float firstBranchHeight = 0.3f; // Where along the trunk branching starts (0 = base, 1 = top)
				int trunkBranches = 2;         // Branches per trunk cylinder
				int branchBranches = 2;        // Branches per cylinder of the first level (deeper levels: half)
				float branchAngle = 50.0f;     // Degrees between a branch and its parent, on average
				float branchLengthRatio = 0.55f; // Length of a branch relative to its parent
				float branchRadiusRatio = 0.55f; // Thickness at its base relative to the parent where it grows
				float upwardTendency = 0.14f;  // Branches turn towards the light
				int ringsPerCylinder = 3;      // Subdivisions along each cylinder, so bends stay round

				// --- Randomness ---
				uint32_t seed = 11;
				float bendiness = 0.09f;       // Random change of direction from one branch cylinder to the next
				float trunkWander = 0.02f;     // Same for the trunk, on top of its curve
				float angleVariation = 0.2f;   // Branch angles vary by this fraction either way
				float lengthVariation = 0.15f; // Branch lengths too
				float positionVariation = 0.3f; // Where along its parent cylinder a branch starts (0 = evenly)
				float azimuthVariation = 0.3f; // Radians added at random to the even spiral of branches
				float leafSizeVariation = 0.2f;
				float leafRollVariation = 35.0f; // Degrees a leaf card turns about its length

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

				// --- Leaves: cards showing a leafy twig (alpha tested), on the thinner branches ---
				// They are a second part of the mesh (material 1; the bark is material 0), each card weighted
				// to the bone of the branch it grows from, so it sways with it.
				bool leaves = true;
				int leafLevel = 2;             // Branch level that carries leaves (0 = trunk)
				int leavesPerCylinder = 2;     // Plus one card at each branch tip
				float leafSize = 0.6f;         // Card length, meters (stem to tip of the texture)
				float leafAspect = 2002.0f / 2392.0f; // Width / length: the texture's aspect ratio
				int leafColumns = 8;           // Card subdivision, for its curl and displacement
				int leafRows = 10;
				float leafCurl = 0.07f;        // Meters the card droops by its tip
				bool leafNormalMapGreenUp = false; // false: DirectX convention
				// Height in [0, 1] at the card's texture coordinates; empty: flat (curled) cards
				std::function<float(float u, float v)> leafHeight;
				float leafDisplacementScale = 0.04f; // Meters between heights 0 and 1
				float leafDisplacementMidlevel = 0.5f;
			};

			SkinnedMesh GenerateTree(const TreeSettings& settings);
		}
	}
}
