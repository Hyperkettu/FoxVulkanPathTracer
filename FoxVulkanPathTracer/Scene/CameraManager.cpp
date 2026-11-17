#include "FoxRenderer.h"

namespace Fox {

	namespace Scene {

		namespace {
			// Centripetal Catmull-Rom (alpha 0.5, Barry-Goldman): the curve between p1 and p2 at t in [0, 1]
			glm::vec3 CatmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
				auto knot = [](const glm::vec3& a, const glm::vec3& b) {
					return std::max(std::sqrt(glm::length(b - a)), 1e-4f);
				};
				const float t0 = 0.0f;
				const float t1 = t0 + knot(p0, p1);
				const float t2 = t1 + knot(p1, p2);
				const float t3 = t2 + knot(p2, p3);
				const float u = glm::mix(t1, t2, t);

				const glm::vec3 a1 = (t1 - u) / (t1 - t0) * p0 + (u - t0) / (t1 - t0) * p1;
				const glm::vec3 a2 = (t2 - u) / (t2 - t1) * p1 + (u - t1) / (t2 - t1) * p2;
				const glm::vec3 a3 = (t3 - u) / (t3 - t2) * p2 + (u - t2) / (t3 - t2) * p3;
				const glm::vec3 b1 = (t2 - u) / (t2 - t0) * a1 + (u - t0) / (t2 - t0) * a2;
				const glm::vec3 b2 = (t3 - u) / (t3 - t1) * a2 + (u - t1) / (t3 - t1) * a3;
				return (t2 - u) / (t2 - t1) * b1 + (u - t1) / (t2 - t1) * b2;
			}

			glm::vec3 ViewDirection(const CameraKey& key) {
				const glm::vec3 direction = key.target - key.position;
				return glm::dot(direction, direction) > 1e-10f ? glm::normalize(direction) : glm::vec3(0.0f, 0.0f, -1.0f);
			}

			// The four control points of segment, from a value of each key; an open path's ends are extended
			// by reflection, so the curve starts and ends heading at its neighbour
			template<typename Value>
			void ControlPoints(int segment, int keyCount, bool loop, Value value, glm::vec3 points[4]) {
				for (int i = 0; i < 4; i++) {
					int index = segment - 1 + i;
					if (loop) {
						points[i] = value(((index % keyCount) + keyCount) % keyCount);
					}
					else if (index < 0) {
						points[i] = 2.0f * value(0) - value(std::min(1, keyCount - 1));
					}
					else if (index >= keyCount) {
						points[i] = 2.0f * value(keyCount - 1) - value(std::max(keyCount - 2, 0));
					}
					else {
						points[i] = value(index);
					}
				}
			}
		}

		// --- CameraPath ---

		int CameraPath::SegmentCount() const {
			const int count = static_cast<int>(keys.size());
			if (count < 2) {
				return 0;
			}
			return loop ? count : count - 1;
		}

		glm::vec3 CameraPath::PositionAt(float u) const {
			const int segments = SegmentCount();
			if (segments == 0) {
				return keys.empty() ? glm::vec3(0.0f) : keys[0].position;
			}
			u = glm::clamp(u, 0.0f, static_cast<float>(segments));
			const int segment = std::min(static_cast<int>(u), segments - 1);
			glm::vec3 points[4];
			ControlPoints(segment, static_cast<int>(keys.size()), loop, [&](int i) { return keys[i].position; }, points);
			return CatmullRom(points[0], points[1], points[2], points[3], u - static_cast<float>(segment));
		}

		glm::vec3 CameraPath::DirectionAt(float u) const {
			const int segments = SegmentCount();
			if (segments == 0) {
				return keys.empty() ? glm::vec3(0.0f, 0.0f, -1.0f) : ViewDirection(keys[0]);
			}
			u = glm::clamp(u, 0.0f, static_cast<float>(segments));
			const int segment = std::min(static_cast<int>(u), segments - 1);
			glm::vec3 points[4];
			ControlPoints(segment, static_cast<int>(keys.size()), loop, [&](int i) { return ViewDirection(keys[i]); }, points);
			const glm::vec3 direction = CatmullRom(points[0], points[1], points[2], points[3], u - static_cast<float>(segment));
			// Opposite neighbouring views would pass through zero: keep the nearer key's view there
			if (glm::dot(direction, direction) < 1e-6f) {
				return points[(u - static_cast<float>(segment)) < 0.5f ? 1 : 2];
			}
			return glm::normalize(direction);
		}

		void CameraPath::ArcTable(std::vector<float>& parameters, std::vector<float>& distances) const {
			const int segments = SegmentCount();
			const int samples = segments * ARC_SAMPLES;
			parameters.resize(static_cast<size_t>(samples) + 1);
			distances.resize(static_cast<size_t>(samples) + 1);

			glm::vec3 previous = PositionAt(0.0f);
			float distance = 0.0f;
			for (int i = 0; i <= samples; i++) {
				const float u = static_cast<float>(i) / static_cast<float>(ARC_SAMPLES);
				const glm::vec3 point = PositionAt(u);
				distance += glm::length(point - previous);
				previous = point;
				parameters[i] = u;
				distances[i] = distance;
			}
		}

		float CameraPath::Length() const {
			if (!IsFlyable()) {
				return 0.0f;
			}
			std::vector<float> parameters, distances;
			ArcTable(parameters, distances);
			return distances.back();
		}

		float CameraPath::Duration() const {
			const float length = Length();
			const float v = std::max(speed, 0.01f);
			if (loop || ease <= 0.0f) {
				return length / v;
			}
			// Constant acceleration for ease seconds at each end; a path too short to reach full speed peaks lower
			const float acceleration = v / ease;
			if (v * ease >= length) {
				return 2.0f * std::sqrt(length / acceleration);
			}
			return 2.0f * ease + (length - v * ease) / v;
		}

		float CameraPath::DistanceAt(float seconds) const {
			const float length = Length();
			if (length <= 0.0f) {
				return 0.0f;
			}
			const float v = std::max(speed, 0.01f);
			seconds = std::max(seconds, 0.0f);

			if (loop) {
				return std::fmod(v * seconds, length);
			}
			if (ease <= 0.0f) {
				return std::min(v * seconds, length);
			}

			const float acceleration = v / ease;
			const float total = Duration();
			if (seconds >= total) {
				return length;
			}
			const float rampTime = (v * ease >= length) ? total * 0.5f : ease; // Time to the peak speed
			const float rampDistance = 0.5f * acceleration * rampTime * rampTime;
			if (seconds < rampTime) {
				return 0.5f * acceleration * seconds * seconds;
			}
			if (seconds > total - rampTime) {
				const float left = total - seconds;
				return length - 0.5f * acceleration * left * left;
			}
			return rampDistance + v * (seconds - rampTime);
		}

		CameraPath::Pose CameraPath::PoseAtDistance(float distance) const {
			Pose pose;
			if (!IsFlyable()) {
				if (!keys.empty()) {
					pose.position = keys[0].position;
					pose.forward = ViewDirection(keys[0]);
				}
				return pose;
			}

			// Spline parameter at this distance along the arc
			std::vector<float> parameters, distances;
			ArcTable(parameters, distances);
			distance = glm::clamp(distance, 0.0f, distances.back());
			const size_t upper = std::min<size_t>(
				std::lower_bound(distances.begin(), distances.end(), distance) - distances.begin(), distances.size() - 1);
			float u = parameters[upper];
			if (upper > 0) {
				const float span = distances[upper] - distances[upper - 1];
				const float f = span > 1e-6f ? (distance - distances[upper - 1]) / span : 0.0f;
				u = glm::mix(parameters[upper - 1], parameters[upper], f);
			}

			pose.position = PositionAt(u);
			if (lookAlongPath) {
				const float step = 1.0f / static_cast<float>(ARC_SAMPLES);
				const float segments = static_cast<float>(SegmentCount());
				float a = u - step, b = u + step;
				if (loop) {
					a = std::fmod(a + segments, segments);
					b = std::fmod(b, segments);
				}
				glm::vec3 tangent = PositionAt(b) - PositionAt(a);
				if (loop && b < a) { // Across the join: around through the end
					tangent = (PositionAt(segments) - PositionAt(a)) + (PositionAt(b) - PositionAt(0.0f));
				}
				pose.forward = glm::dot(tangent, tangent) > 1e-10f ? glm::normalize(tangent) : DirectionAt(u);
			}
			else {
				pose.forward = DirectionAt(u);
			}
			return pose;
		}

		CameraPath::Pose CameraPath::PoseAt(float seconds) const {
			return PoseAtDistance(DistanceAt(seconds));
		}

		std::vector<glm::vec3> CameraPath::Polyline(int pointsPerSegment) const {
			std::vector<glm::vec3> points;
			const int segments = SegmentCount();
			if (segments == 0) {
				return points;
			}
			points.reserve(static_cast<size_t>(segments * pointsPerSegment) + 1);
			for (int i = 0; i <= segments * pointsPerSegment; i++) {
				points.push_back(PositionAt(static_cast<float>(i) / static_cast<float>(pointsPerSegment)));
			}
			return points;
		}

		// --- CameraManager ---

		void CameraManager::SetActivePath(int index) {
			index = paths.empty() ? -1 : glm::clamp(index, 0, static_cast<int>(paths.size()) - 1);
			if (index != activePath) {
				activePath = index;
				playing = false;
				time = 0.0f;
			}
		}

		CameraPath* CameraManager::ActivePath() {
			return activePath >= 0 && activePath < static_cast<int>(paths.size()) ? &paths[activePath] : nullptr;
		}

		CameraPath& CameraManager::AddPath() {
			CameraPath path;
			path.name = "Path " + std::to_string(paths.size() + 1);
			paths.push_back(std::move(path));
			SetActivePath(static_cast<int>(paths.size()) - 1);
			return paths.back();
		}

		void CameraManager::RemovePath(int index) {
			if (index < 0 || index >= static_cast<int>(paths.size())) {
				return;
			}
			paths.erase(paths.begin() + index);
			playing = false;
			time = 0.0f;
			activePath = paths.empty() ? -1 : std::min(activePath, static_cast<int>(paths.size()) - 1);
		}

		void CameraManager::Clear() {
			paths.clear();
			activePath = -1;
			playing = false;
			time = 0.0f;
		}

		bool CameraManager::Play() {
			const CameraPath* path = ActivePath();
			if (!path || !path->IsFlyable()) {
				return false;
			}
			if (!path->loop && time >= path->Duration() - 1e-4f) {
				time = 0.0f; // Ended: from the start again
			}
			playing = true;
			return true;
		}

		bool CameraManager::TogglePlay() {
			if (playing) {
				Stop();
				return false;
			}
			return Play();
		}

		void CameraManager::ApplyPose(float seconds, Fox::Graphics::Vulkan::Camera& camera) const {
			if (activePath < 0 || activePath >= static_cast<int>(paths.size())) {
				return;
			}
			const CameraPath::Pose pose = paths[activePath].PoseAt(seconds);

			// Straight up or down has no yaw: stay just short of it
			glm::vec3 forward = pose.forward;
			forward.y = glm::clamp(forward.y, -0.995f, 0.995f);
			forward = glm::normalize(forward);

			camera.SetPosition(pose.position);
			camera.SetCameraTarget(pose.position + forward);
		}

		bool CameraManager::Update(float deltaTime, const glm::vec3& move, Fox::Graphics::Vulkan::Camera& camera) {
			if (playing) {
				const CameraPath* path = ActivePath();
				if (!path || !path->IsFlyable()) {
					playing = false;
					return false;
				}
				const float duration = path->Duration();
				time += deltaTime;
				if (path->loop) {
					time = duration > 0.0f ? std::fmod(time, duration) : 0.0f;
				}
				else if (time >= duration) {
					time = duration;
					playing = false;
				}
				ApplyPose(time, camera);
				return true;
			}

			if (move == glm::vec3(0.0f)) {
				return false;
			}
			const float distance = flySpeed * deltaTime;
			camera.MoveRight(move.x * distance);
			camera.MoveUp(move.y * distance);
			camera.MoveForward(move.z * distance);
			return true;
		}
	}
}
