#pragma once

#include <vector>
#include <glm/glm.hpp>

namespace Fox {

	namespace Animation {

		// A tree growing from a sprout: its bones grow out from the root in order along the tree (a growth front
		// moving out at constant speed: the trunk rises and branches sprout where it has passed, each bone
		// lengthening and thickening from its base), the whole tree enlarging from startScale meanwhile, and the
		// leaf cards unfolding from their stems leafLag behind the branches (in the skinning shader).
		struct TreeGrowth {
			bool enabled = false;      // Off: grown
			float duration = 20.0f;    // Seconds from sprout to grown
			float startScale = 0.08f;  // Size of the sprout, relative to the grown tree
			float leafLag = 0.3f;      // How far behind the branches the leaves are (a fraction of the tree)
			bool loop = false;         // Starts again once grown
		};

		// Moves a rig of branches (see TreeGenerator) in the wind. The wind's steady push, which bends a tree downwind,
		// is left to Lean: instances of one tree share its rig but not its rotation, so a downwind bend computed in
		// the mesh would point every rotated instance a different way. The rig only flutters: gusty turbulence across
		// and along the branches, with no direction of its own.
		//
		// Every joint is an angular spring: the bone
		// is turned away from its rest angle by the wind's torque and pulled back in proportion to how far it
		// turned, with damping:
		//
		//     I * angularAcceleration = windTorque - k * angle - c * angularVelocity
		//
		// The torque at a joint is the drag on everything it carries (its whole subtree), about the joint.
		// Stiffness comes from the natural frequency of each branch: thick branches sway slowly, twigs quickly.
		// Angles are small, so forces and lever arms are taken in the rest pose.
		class WindSkeleton {
		public:
			struct Settings {
				glm::vec3 windDirection{ 1.0f, 0.0f, 0.35f }; // World space, horizontal part is used (the scene sets it: one wind)
				float windSpeed = 8.0f;           // m/s, mean
				float gustStrength = 0.7f;        // Gusts reach (1 + this) times the mean speed
				float gustSpeed = 6.0f;           // m/s: how fast gusts travel across the tree
				float turbulence = 0.35f;         // Fluttering across the wind, relative to the mean speed

				float trunkFrequency = 0.45f;     // Hz, swaying of the trunk
				float maxFrequency = 3.5f;        // Hz, the thinnest twigs
				float dampingRatio = 0.12f;       // 0: swings forever, 1: no overshoot
				float response = 5.0f;            // Scales the wind load (bare branches catch little wind; this
				                                  // stands in for foliage and fine twigs)
				float maxAngle = 0.3f;            // Radians a joint can bend from rest
			};

			WindSkeleton(const std::vector<Fox::Graphics::Geometry::Bone>& bones, const Settings& settings = Settings());

			Settings& GetSettings() { return settings; }

			// Advances the springs. time: seconds since start (drives the gusts). objectToWorld: where the tree
			// stands; the wind blows in world space.
			void Update(float deltaTime, float time, const glm::mat4& objectToWorld);

			// A whole tree swaying downwind about its base: a damped spring at the trunk's frequency, pushed by the
			// gusty wind where the tree stands (gusts roll across a forest). One per instance; the scene tilts the
			// instance by angle radians towards the wind.
			struct Lean {
				float angle = 0.0f;
				float velocity = 0.0f;
			};
			static void UpdateLean(Lean& lean, const Settings& settings, const glm::vec3& worldBase, float time, float deltaTime);
			// The angle the gusty wind pushes a plant at worldBase to (radians, towards the wind), with no spring
			// (grass and other clutter: light and quick, it follows the gusts)
			static float LeanTarget(const Settings& settings, const glm::vec3& worldBase, float time);
			// The gusty wind's speed at a point (m/s): the mean speed times 1 + gustStrength * the gust passing there
			static float GustSpeed(const Settings& settings, const glm::vec3& worldPosition, float time);

			// Per bone: pose * inverse(bind), in mesh space (for RayTracingScene::SetJointMatrices)
			const std::vector<glm::mat4>& GetSkinMatrices() const { return skinMatrices; }

			// How grown it is, from 0 (a sprout) to 1 (grown), for the next Update
			void SetGrowth(float progress, const TreeGrowth& growth);
			// The leaves' growth front for the skinning shader (RayTracingScene::SetSkinGrowth): leaves whose stem
			// is less than front metres along the tree have grown, over span metres; 1e30 when grown
			float GetLeafFront() const { return leafFront; }
			float GetLeafSpan() const { return leafSpan; }

		private:
			struct Joint {
				int parent = -1;
				glm::mat4 restLocal{ 1.0f };   // Relative to the parent's bind frame
				glm::mat4 inverseBind{ 1.0f };
				glm::mat3 bindRotation{ 1.0f }; // Bone frame in mesh space
				glm::vec3 origin{ 0.0f };      // Joint position, mesh space
				glm::vec3 axis{ 0.0f, 1.0f, 0.0f };
				glm::vec3 center{ 0.0f };      // Middle of the bone, where its drag acts
				float length = 1.0f;
				float radius = 0.1f;
				float mass = 1.0f;

				float frequency = 1.0f;  // rad/s
				float inertia = 1.0f;    // Of the subtree about the joint
				float phase = 0.0f;      // Decorrelates the flutter of branches

				glm::vec3 angle{ 0.0f };    // Rotation vector in the bone's frame
				glm::vec3 velocity{ 0.0f };

				float distance = 0.0f;   // Metres along the tree from the root to its start
				float growth = 1.0f;     // Its own scale while growing (its subtree's too)
			};
			float rootScale = 1.0f;
			float treeLength = 1.0f;     // The furthest tip, metres along the tree
			float leafFront = 1e30f, leafSpan = 1.0f;

			Settings settings;
			std::vector<Joint> joints;
			std::vector<glm::mat4> skinMatrices;
			std::vector<glm::mat4> pose;
		};
	}
}
