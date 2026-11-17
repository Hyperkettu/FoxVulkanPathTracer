#pragma once

#include <vector>
#include <glm/glm.hpp>

namespace Fox {

	namespace Animation {

		// Moves a rig of branches (see TreeGenerator) in the wind. Every joint is an angular spring: the bone
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
				glm::vec3 windDirection{ 1.0f, 0.0f, 0.35f }; // World space, horizontal part is used
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

			// Per bone: pose * inverse(bind), in mesh space (for RayTracingScene::SetJointMatrices)
			const std::vector<glm::mat4>& GetSkinMatrices() const { return skinMatrices; }

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
			};

			Settings settings;
			std::vector<Joint> joints;
			std::vector<glm::mat4> skinMatrices;
			std::vector<glm::mat4> pose;
		};
	}
}
