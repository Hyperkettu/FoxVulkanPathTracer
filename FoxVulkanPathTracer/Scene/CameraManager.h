#pragma once

#include <string>
#include <vector>

namespace Fox {

	namespace Scene {

		// One point of a camera path: where the camera is and a point it looks at
		struct CameraKey {
			glm::vec3 position{ 0.0f };
			glm::vec3 target{ 0.0f, 0.0f, -1.0f };
		};

		// A camera flight through keys, joined by a centripetal Catmull-Rom spline (it passes through every key,
		// without loops or overshoot where keys are unevenly spaced). The camera flies it at a constant speed,
		// whatever the spacing of the keys, speeding up from rest and slowing down to it at the ends.
		struct CameraPath {
			std::string name = "Path";
			std::vector<CameraKey> keys;
			float speed = 4.0f;         // m/s
			float ease = 1.5f;          // s from rest to full speed, and back to rest at the end (0: none)
			bool loop = false;          // The last key joins the first and the flight goes round until stopped
			bool lookAlongPath = false; // Look where the path goes instead of at the keys' targets

			struct Pose {
				glm::vec3 position{ 0.0f };
				glm::vec3 forward{ 0.0f, 0.0f, -1.0f };
			};

			bool IsFlyable() const { return keys.size() >= 2; }
			float Length() const;
			float Duration() const;                // s for one flight (one round of a loop)
			float DistanceAt(float seconds) const; // m from the first key, with easing; loops wrap around
			Pose PoseAt(float seconds) const;
			Pose PoseAtDistance(float distance) const;

			// Points along the spline, for drawing it
			std::vector<glm::vec3> Polyline(int pointsPerSegment) const;

		private:
			int SegmentCount() const;
			const CameraKey& Key(int index) const; // Wraps (loop) or clamps (open)
			glm::vec3 PositionAt(float u) const;   // u: segment index + fraction, 0 ... SegmentCount()
			glm::vec3 DirectionAt(float u) const;  // Unit view direction between the keys' views
			// Spline parameter u at each of ARC_SAMPLES samples per segment, and the distance to it
			void ArcTable(std::vector<float>& parameters, std::vector<float>& distances) const;
			static constexpr int ARC_SAMPLES = 48;
		};

		// The scene's camera paths and the camera's flight: free flight from input, or along the active path
		// while it plays.
		class CameraManager {
		public:
			std::vector<CameraPath>& GetPaths() { return paths; }
			const std::vector<CameraPath>& GetPaths() const { return paths; }

			int GetActivePath() const { return activePath; }
			void SetActivePath(int index);
			CameraPath* ActivePath();

			// A new path, made the active one
			CameraPath& AddPath();
			void RemovePath(int index);
			void Clear();

			// Plays the active path from where it is (from the start once it has ended); false if it cannot be flown
			bool Play();
			void Stop() { playing = false; }
			bool TogglePlay();
			bool IsPlaying() const { return playing; }

			// Position in the active path's flight
			float GetTime() const { return time; }
			void SetTime(float seconds) { time = std::max(seconds, 0.0f); }

			// Moves the camera: along the active path while playing, otherwise by move (right, up, forward, each
			// -1 ... 1) at the flight speed. Returns true when the camera moved.
			bool Update(float deltaTime, const glm::vec3& move, Fox::Graphics::Vulkan::Camera& camera);

			// Puts the camera where the active path is at seconds
			void ApplyPose(float seconds, Fox::Graphics::Vulkan::Camera& camera) const;

			float flySpeed = 10.0f; // m/s, free flight

		private:
			std::vector<CameraPath> paths;
			int activePath = -1;
			bool playing = false;
			float time = 0.0f;
		};
	}
}
