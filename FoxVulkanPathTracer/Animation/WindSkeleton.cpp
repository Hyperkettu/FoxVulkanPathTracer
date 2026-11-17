#include "FoxRenderer.h"

namespace Fox {

	namespace Animation {

		namespace {
			const float WOOD_DENSITY = 700.0f; // kg/m^3
			const float AIR_DENSITY = 1.2f;    // kg/m^3
			const float DRAG_COEFFICIENT = 1.2f; // A cylinder across the flow
			const float SUBSTEP = 1.0f / 240.0f; // Integration step, well below the fastest twig's period

			// Smooth, irregular variation in [-1, 1]: sums of incommensurate sines
			float Gust(float t) {
				const float tau = glm::two_pi<float>();
				return 0.6f * std::sin(tau * 0.11f * t) + 0.3f * std::sin(tau * 0.27f * t + 1.3f) + 0.1f * std::sin(tau * 0.73f * t + 2.1f);
			}
		}

		WindSkeleton::WindSkeleton(const std::vector<Fox::Graphics::Geometry::Bone>& bones, const Settings& settings)
			: settings(settings)
		{
			joints.resize(bones.size());
			skinMatrices.assign(bones.size(), glm::mat4(1.0f));
			pose.assign(bones.size(), glm::mat4(1.0f));

			const float rootRadius = bones.empty() ? 1.0f : bones[0].radius;

			for (size_t i = 0; i < bones.size(); i++) {
				const auto& bone = bones[i];
				Joint& joint = joints[i];

				const glm::mat4 bind = bone.BindMatrix();
				joint.parent = bone.parent;
				joint.inverseBind = glm::inverse(bind);
				joint.restLocal = bone.parent >= 0 ? glm::inverse(bones[bone.parent].BindMatrix()) * bind : bind;
				joint.bindRotation = glm::mat3_cast(bone.rotation);
				joint.origin = bone.origin;
				joint.axis = joint.bindRotation * glm::vec3(0.0f, 1.0f, 0.0f);
				joint.center = bone.origin + joint.axis * (bone.length * 0.5f);
				joint.length = bone.length;
				joint.radius = bone.radius;
				joint.mass = WOOD_DENSITY * glm::pi<float>() * bone.radius * bone.radius * bone.length;

				// Thinner branches sway faster
				const float hertz = glm::clamp(settings.trunkFrequency * std::pow(rootRadius / std::max(bone.radius, 1e-3f), 0.6f),
					settings.trunkFrequency, settings.maxFrequency);
				joint.frequency = glm::two_pi<float>() * hertz;
				joint.phase = static_cast<float>(i) * 2.399963f; // Golden angle: well spread
			}

			// Inertia of each subtree about its joint: sum m |c - p|^2 = S2 - 2 p.S1 + |p|^2 S0, with the sums
			// S0 = sum m, S1 = sum m c, S2 = sum m |c|^2 gathered from the leaves down (children come after parents)
			std::vector<float> s0(joints.size()), s2(joints.size());
			std::vector<glm::vec3> s1(joints.size());
			for (size_t i = 0; i < joints.size(); i++) {
				s0[i] = joints[i].mass;
				s1[i] = joints[i].mass * joints[i].center;
				s2[i] = joints[i].mass * glm::dot(joints[i].center, joints[i].center);
			}
			for (size_t i = joints.size(); i-- > 0;) {
				const glm::vec3& p = joints[i].origin;
				joints[i].inertia = std::max(s2[i] - 2.0f * glm::dot(p, s1[i]) + glm::dot(p, p) * s0[i], 1e-6f);

				const int parent = joints[i].parent;
				if (parent >= 0) {
					s0[parent] += s0[i];
					s1[parent] += s1[i];
					s2[parent] += s2[i];
				}
			}
		}

		void WindSkeleton::Update(float deltaTime, float time, const glm::mat4& objectToWorld) {
			if (joints.empty() || deltaTime <= 0.0f) {
				return;
			}

			// --- Wind in mesh space ---
			glm::vec3 windWorld(settings.windDirection.x, 0.0f, settings.windDirection.z);
			windWorld = glm::dot(windWorld, windWorld) > 1e-8f ? glm::normalize(windWorld) : glm::vec3(1.0f, 0.0f, 0.0f);

			const glm::mat3 toMesh = glm::inverse(glm::mat3(objectToWorld));
			const glm::vec3 wind = glm::normalize(toMesh * windWorld);
			const glm::vec3 up = glm::normalize(toMesh * glm::vec3(0.0f, 1.0f, 0.0f));
			const glm::vec3 across = glm::normalize(glm::cross(up, wind));

			// --- Drag on every bone, then gathered into each joint's torque ---
			std::vector<glm::vec3> force(joints.size());
			std::vector<glm::vec3> moment(joints.size()); // About the mesh origin: sum c x F

			for (size_t i = 0; i < joints.size(); i++) {
				const Joint& joint = joints[i];

				// Gusts roll across the tree with the wind
				const glm::vec3 worldCenter = glm::vec3(objectToWorld * glm::vec4(joint.center, 1.0f));
				const float gustTime = time - glm::dot(worldCenter, windWorld) / std::max(settings.gustSpeed, 0.1f);
				const float speed = settings.windSpeed * std::max(1.0f + settings.gustStrength * Gust(gustTime), 0.0f);

				const float flutter = settings.windSpeed * settings.turbulence;
				const glm::vec3 velocity = wind * speed
					+ across * (flutter * std::sin(glm::two_pi<float>() * 1.7f * time + joint.phase))
					+ up * (0.5f * flutter * std::sin(glm::two_pi<float>() * 2.3f * time + 2.0f * joint.phase));

				// Only the flow across the bone pushes it: quadratic drag on its projected area
				const glm::vec3 normalFlow = velocity - joint.axis * glm::dot(velocity, joint.axis);
				const float area = 2.0f * joint.radius * joint.length;
				force[i] = (0.5f * AIR_DENSITY * DRAG_COEFFICIENT * area * settings.response) * glm::length(normalFlow) * normalFlow;
				moment[i] = glm::cross(joint.center, force[i]);
			}

			std::vector<glm::vec3> torque(joints.size());
			for (size_t i = joints.size(); i-- > 0;) {
				// sum over the subtree of (c - p) x F = sum c x F - p x sum F
				torque[i] = moment[i] - glm::cross(joints[i].origin, force[i]);

				const int parent = joints[i].parent;
				if (parent >= 0) {
					force[parent] += force[i];
					moment[parent] += moment[i];
				}
			}

			// In each bone's own frame; branches bend but do not twist about their length
			for (size_t i = 0; i < joints.size(); i++) {
				torque[i] = glm::transpose(joints[i].bindRotation) * torque[i];
				torque[i].y = 0.0f;
			}

			// --- Springs ---
			const int substeps = std::clamp(static_cast<int>(std::ceil(deltaTime / SUBSTEP)), 1, 32);
			const float h = deltaTime / static_cast<float>(substeps);

			for (Joint& joint : joints) {
				const size_t i = static_cast<size_t>(&joint - joints.data());
				const float w = joint.frequency;
				const glm::vec3 driving = torque[i] / joint.inertia;

				for (int step = 0; step < substeps; step++) {
					// Semi-implicit Euler: stable for a damped spring at this step size
					const glm::vec3 acceleration = driving - (w * w) * joint.angle - (2.0f * settings.dampingRatio * w) * joint.velocity;
					joint.velocity += acceleration * h;
					joint.angle += joint.velocity * h;
				}

				const float bent = glm::length(joint.angle);
				if (bent > settings.maxAngle) {
					joint.angle *= settings.maxAngle / bent;
					joint.velocity -= joint.angle * (glm::dot(joint.velocity, joint.angle) / glm::dot(joint.angle, joint.angle));
				}
			}

			// --- Pose: parents first ---
			for (size_t i = 0; i < joints.size(); i++) {
				const Joint& joint = joints[i];

				glm::mat4 bend(1.0f);
				const float bent = glm::length(joint.angle);
				if (bent > 1e-7f) {
					bend = glm::rotate(glm::mat4(1.0f), bent, joint.angle / bent);
				}

				pose[i] = (joint.parent >= 0 ? pose[joint.parent] : glm::mat4(1.0f)) * joint.restLocal * bend;
				skinMatrices[i] = pose[i] * joint.inverseBind;
			}
		}
	}
}
