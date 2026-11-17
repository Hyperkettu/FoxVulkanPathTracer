#include "FoxRenderer.h"

namespace Fox {

	namespace Editor {

		namespace {
			const glm::vec4 AXIS_COLORS[3] = {
				{ 0.96f, 0.26f, 0.26f, 1.0f }, // X
				{ 0.40f, 0.86f, 0.28f, 1.0f }, // Y
				{ 0.28f, 0.52f, 1.00f, 1.0f }, // Z
			};
			const glm::vec4 CENTER_COLOR{ 0.95f, 0.95f, 0.95f, 1.0f };
			const glm::vec4 HIGHLIGHT_COLOR{ 1.0f, 0.84f, 0.18f, 1.0f };
			const glm::vec4 OUTLINE_COLOR{ 0.0f, 0.0f, 0.0f, 0.55f };

			// Sizes in pixels at 1080 lines
			const float LINE_WIDTH = 2.5f;
			const float OUTLINE_WIDTH = 2.5f;  // Added around lines so they read on bright and dark backgrounds
			const float GRAB_DISTANCE = 10.0f; // How close the mouse must be to a handle
			const float ARROW_WIDTH = 7.0f;
			const float SQUARE_SIZE = 6.0f;    // Half size
			const float CENTER_SIZE = 7.0f;

			const float HANDLE_SCREEN_SIZE = 0.3f; // Handle length relative to half the image height
			const int RING_SEGMENTS = 72;

			const float SNAP_DISTANCE = 0.25f;
			const float SNAP_ANGLE = glm::radians(15.0f);
			const float SNAP_SCALE = 0.1f;

			float SegmentDistance(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
				const glm::vec2 ab = b - a;
				const float lengthSquared = glm::dot(ab, ab);
				const float t = lengthSquared > 0.0f ? glm::clamp(glm::dot(p - a, ab) / lengthSquared, 0.0f, 1.0f) : 0.0f;
				return glm::length(p - (a + ab * t));
			}

			float Snap(float value, float step) {
				return std::round(value / step) * step;
			}

			using Vertices = std::vector<TransformGizmo::OverlayVertex>;

			void AddTriangle(Vertices& out, glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& color) {
				out.push_back({ a, color });
				out.push_back({ b, color });
				out.push_back({ c, color });
			}

			void AddLine(Vertices& out, glm::vec2 a, glm::vec2 b, float width, const glm::vec4& color) {
				const glm::vec2 d = b - a;
				const float length = glm::length(d);
				if (length < 1e-3f) {
					return;
				}
				// Extended by half the width at both ends so connected segments join without gaps
				const glm::vec2 along = d / length * (width * 0.5f);
				const glm::vec2 side(-along.y, along.x);
				const glm::vec2 a0 = a - along + side, a1 = a - along - side;
				const glm::vec2 b0 = b + along + side, b1 = b + along - side;
				AddTriangle(out, a0, a1, b0, color);
				AddTriangle(out, a1, b1, b0, color);
			}

			void AddSquare(Vertices& out, glm::vec2 center, float halfSize, const glm::vec4& color) {
				const glm::vec2 a = center + glm::vec2(-halfSize, -halfSize);
				const glm::vec2 b = center + glm::vec2(halfSize, -halfSize);
				const glm::vec2 c = center + glm::vec2(halfSize, halfSize);
				const glm::vec2 d = center + glm::vec2(-halfSize, halfSize);
				AddTriangle(out, a, b, c, color);
				AddTriangle(out, a, c, d, color);
			}

			glm::vec4 WithAlpha(glm::vec4 color, float alpha) {
				color.a *= alpha;
				return color;
			}
		}

		const char* TransformGizmo::ModeName(Mode mode) {
			switch (mode) {
			case Mode::Translate: return "Move";
			case Mode::Rotate: return "Rotate";
			case Mode::Scale: return "Scale";
			}
			return "";
		}

		bool TransformGizmo::Project(const View& view, const glm::vec3& point, glm::vec2& pixel) {
			const glm::vec3 relative = point - view.position;
			const float depth = glm::dot(relative, view.forward);
			if (depth < 1e-3f) {
				return false;
			}

			// The inverse of the path tracer's primary ray directions (PrimaryRayDirection in raygen.slang)
			const float aspect = view.size.x / view.size.y;
			const float x = glm::dot(relative, view.right) / (depth * view.tanHalfFov * aspect);
			const float y = glm::dot(relative, view.up) / (depth * view.tanHalfFov);
			pixel = glm::vec2((x * 0.5f + 0.5f) * view.size.x, (0.5f - y * 0.5f) * view.size.y);
			return true;
		}

		void TransformGizmo::MouseRay(const View& view, glm::vec2 mouse, glm::vec3& origin, glm::vec3& direction) {
			glm::vec2 d = mouse / view.size * 2.0f - 1.0f;
			d.y = -d.y;
			const float aspect = view.size.x / view.size.y;
			origin = view.position;
			direction = glm::normalize(view.forward + view.right * (d.x * view.tanHalfFov * aspect) + view.up * (d.y * view.tanHalfFov));
		}

		bool TransformGizmo::AxisParameter(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& point, const glm::vec3& axis, float& parameter) {
			// Point on the line point + s * axis closest to the ray
			const glm::vec3 w = point - rayOrigin;
			const float b = glm::dot(axis, rayDirection);
			const float denominator = 1.0f - b * b;
			if (denominator < 1e-4f) {
				return false; // The axis points along the view: no usable position on it
			}
			parameter = (b * glm::dot(rayDirection, w) - glm::dot(axis, w)) / denominator;
			return true;
		}

		TransformGizmo::Frame TransformGizmo::FrameOf(const View& view, const Fox::Scene::RayTracing::RayTracingScene& scene) const {
			const auto& transform = scene.GetObjectTransform(static_cast<size_t>(selected));

			Frame frame;
			frame.pivot = transform.position;

			if (mode == Mode::Scale) {
				const glm::mat3 rotation = glm::mat3_cast(transform.rotation);
				for (int axis = 0; axis < 3; axis++) {
					frame.axes[axis] = rotation[axis];
				}
			}
			else {
				frame.axes[0] = glm::vec3(1, 0, 0);
				frame.axes[1] = glm::vec3(0, 1, 0);
				frame.axes[2] = glm::vec3(0, 0, 1);
			}

			const float depth = std::max(glm::dot(frame.pivot - view.position, view.forward), 0.1f);
			frame.length = depth * view.tanHalfFov * HANDLE_SCREEN_SIZE;
			frame.uiScale = view.size.y / 1080.0f;
			return frame;
		}

		std::vector<glm::vec3> TransformGizmo::RingPoints(const Frame& frame, int axis) const {
			const glm::vec3& u = frame.axes[(axis + 1) % 3];
			const glm::vec3& w = frame.axes[(axis + 2) % 3];

			std::vector<glm::vec3> points(RING_SEGMENTS + 1);
			for (int i = 0; i <= RING_SEGMENTS; i++) {
				const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(RING_SEGMENTS);
				points[i] = frame.pivot + (u * std::cos(angle) + w * std::sin(angle)) * frame.length;
			}
			return points;
		}

		TransformGizmo::Handle TransformGizmo::HandleAt(glm::vec2 mouse, const View& view, const Frame& frame) const {
			glm::vec2 center;
			if (!Project(view, frame.pivot, center)) {
				return Handle::None;
			}

			const float grab = GRAB_DISTANCE * frame.uiScale;

			if (mode != Mode::Rotate) {
				const glm::vec2 offset = glm::abs(mouse - center);
				if (std::max(offset.x, offset.y) <= CENTER_SIZE * frame.uiScale + 3.0f) {
					return Handle::Center;
				}
			}

			Handle best = Handle::None;
			float bestDistance = grab;

			for (int axis = 0; axis < 3; axis++) {
				float distance = std::numeric_limits<float>::max();

				if (mode == Mode::Rotate) {
					const std::vector<glm::vec3> ring = RingPoints(frame, axis);
					for (size_t i = 0; i + 1 < ring.size(); i++) {
						glm::vec2 a, b;
						if (Project(view, ring[i], a) && Project(view, ring[i + 1], b)) {
							distance = std::min(distance, SegmentDistance(mouse, a, b));
						}
					}
				}
				else {
					glm::vec2 tip;
					if (Project(view, frame.pivot + frame.axes[axis] * frame.length, tip)) {
						distance = SegmentDistance(mouse, center, tip);
					}
				}

				if (distance < bestDistance) {
					bestDistance = distance;
					best = static_cast<Handle>(axis);
				}
			}

			return best;
		}

		bool TransformGizmo::MouseDown(glm::vec2 mouse, const View& view, Fox::Scene::RayTracing::RayTracingScene& scene) {
			if (selected < 0 || !scene.IsObjectMovable(static_cast<size_t>(selected))) {
				return false;
			}

			const Frame frame = FrameOf(view, scene);
			const Handle handle = HandleAt(mouse, view, frame);
			if (handle == Handle::None) {
				return false;
			}

			dragging = true;
			active = handle;
			hovered = handle;
			start = scene.GetObjectTransform(static_cast<size_t>(selected));
			startFrame = frame;
			startMouse = mouse;
			startParameter = 0.0f;

			if (mode == Mode::Rotate) {
				// The point of the ring nearest to the mouse, and which way the ring runs on screen there.
				// Increasing ring angle is a positive (right-handed) rotation about the axis.
				const std::vector<glm::vec3> ring = RingPoints(frame, static_cast<int>(handle));
				float nearest = std::numeric_limits<float>::max();
				for (size_t i = 0; i + 1 < ring.size(); i++) {
					glm::vec2 a, b;
					if (!Project(view, ring[i], a) || !Project(view, ring[i + 1], b)) {
						continue;
					}
					const float distance = SegmentDistance(mouse, a, b);
					const float stepRadians = glm::two_pi<float>() / static_cast<float>(RING_SEGMENTS);
					if (distance < nearest && glm::length(b - a) > 1e-3f) {
						nearest = distance;
						ringTangent = glm::normalize(b - a);
						pixelsPerRadian = glm::length(b - a) / stepRadians;
					}
				}

				// Seen edge-on the ring barely moves on screen near its ends: keep the turn rate usable
				glm::vec2 center, tip;
				if (Project(view, frame.pivot, center) && Project(view, frame.pivot + view.up * frame.length, tip)) {
					pixelsPerRadian = std::max(pixelsPerRadian, 0.5f * glm::length(tip - center));
				}
			}

			glm::vec3 origin, direction;
			MouseRay(view, mouse, origin, direction);

			if (handle == Handle::Center) {
				// Moving freely: in the plane through the object facing the camera
				const float denominator = glm::dot(direction, view.forward);
				const float t = std::abs(denominator) > 1e-6f ? glm::dot(frame.pivot - origin, view.forward) / denominator : 0.0f;
				startPlanePoint = origin + direction * t;
			}
			else if (mode != Mode::Rotate) {
				AxisParameter(origin, direction, frame.pivot, frame.axes[static_cast<int>(handle)], startParameter);
			}

			return true;
		}

		void TransformGizmo::MouseMove(glm::vec2 mouse, const View& view, Fox::Scene::RayTracing::RayTracingScene& scene, bool snap) {
			if (selected < 0) {
				hovered = Handle::None;
				return;
			}

			if (!dragging) {
				hovered = scene.IsObjectMovable(static_cast<size_t>(selected)) ? HandleAt(mouse, view, FrameOf(view, scene)) : Handle::None;
				return;
			}

			glm::vec3 origin, direction;
			MouseRay(view, mouse, origin, direction);

			auto transform = start;
			const int axis = static_cast<int>(active);

			switch (mode) {
			case Mode::Translate:
				if (active == Handle::Center) {
					const float denominator = glm::dot(direction, view.forward);
					if (std::abs(denominator) < 1e-6f) {
						return;
					}
					const glm::vec3 point = origin + direction * (glm::dot(startFrame.pivot - origin, view.forward) / denominator);
					glm::vec3 delta = point - startPlanePoint;
					if (snap) {
						delta = glm::vec3(Snap(delta.x, SNAP_DISTANCE), Snap(delta.y, SNAP_DISTANCE), Snap(delta.z, SNAP_DISTANCE));
					}
					transform.position = start.position + delta;
				}
				else {
					float parameter;
					if (!AxisParameter(origin, direction, startFrame.pivot, startFrame.axes[axis], parameter)) {
						return;
					}
					float delta = parameter - startParameter;
					if (snap) {
						delta = Snap(delta, SNAP_DISTANCE);
					}
					transform.position = start.position + startFrame.axes[axis] * delta;
				}
				break;

			case Mode::Rotate: {
				// Dragging along the ring turns the object as if the grabbed point of the ring followed the mouse
				float angle = glm::dot(mouse - startMouse, ringTangent) / pixelsPerRadian;
				if (snap) {
					angle = Snap(angle, SNAP_ANGLE);
				}
				transform.rotation = glm::normalize(glm::angleAxis(angle, startFrame.axes[axis]) * start.rotation);
				break;
			}

			case Mode::Scale:
				if (active == Handle::Center) {
					float factor = std::exp((mouse.x - startMouse.x) / (250.0f * startFrame.uiScale));
					if (snap) {
						factor = std::max(Snap(factor, SNAP_SCALE), SNAP_SCALE);
					}
					transform.scale = start.scale * factor;
				}
				else {
					float parameter;
					if (!AxisParameter(origin, direction, startFrame.pivot, startFrame.axes[axis], parameter)) {
						return;
					}
					float factor = 1.0f + (parameter - startParameter) / startFrame.length;
					if (snap) {
						factor = Snap(factor, SNAP_SCALE);
					}
					transform.scale[axis] = start.scale[axis] * std::max(factor, 0.01f);
				}
				break;
			}

			scene.SetObjectTransform(static_cast<size_t>(selected), transform);
		}

		void TransformGizmo::BuildOverlay(const View& view, const Fox::Scene::RayTracing::RayTracingScene& scene, std::vector<OverlayVertex>& vertices) const {
			if (selected < 0 || !scene.IsObjectMovable(static_cast<size_t>(selected))) {
				return;
			}

			const Frame frame = FrameOf(view, scene);
			glm::vec2 center;
			if (!Project(view, frame.pivot, center)) {
				return;
			}

			const float ui = frame.uiScale;
			const float lineWidth = LINE_WIDTH * ui;
			const float outlineWidth = lineWidth + OUTLINE_WIDTH * ui;
			const Handle highlighted = dragging ? active : hovered;

			auto colorOf = [&](Handle handle) {
				if (handle == highlighted) return HIGHLIGHT_COLOR;
				return handle == Handle::Center ? CENTER_COLOR : AXIS_COLORS[static_cast<int>(handle)];
			};

			// Outlines first, then the colours on top
			for (int pass = 0; pass < 2; pass++) {
				const bool outline = pass == 0;
				const float width = outline ? outlineWidth : lineWidth;
				const float grow = outline ? OUTLINE_WIDTH * 0.5f * ui : 0.0f;

				for (int axis = 0; axis < 3; axis++) {
					const Handle handle = static_cast<Handle>(axis);
					const glm::vec4 color = outline ? OUTLINE_COLOR : colorOf(handle);

					if (mode == Mode::Rotate) {
						// The half of the ring behind the object is dimmed
						const std::vector<glm::vec3> ring = RingPoints(frame, axis);
						for (size_t i = 0; i + 1 < ring.size(); i++) {
							glm::vec2 a, b;
							if (!Project(view, ring[i], a) || !Project(view, ring[i + 1], b)) {
								continue;
							}
							const glm::vec3 middle = (ring[i] + ring[i + 1]) * 0.5f;
							const bool front = glm::dot(middle - frame.pivot, view.position - frame.pivot) >= 0.0f;
							AddLine(vertices, a, b, width, WithAlpha(color, front ? 1.0f : 0.35f));
						}
						continue;
					}

					const glm::vec3 tipPoint = frame.pivot + frame.axes[axis] * frame.length;
					glm::vec2 tip;
					if (!Project(view, tipPoint, tip)) {
						continue;
					}

					if (mode == Mode::Translate) {
						glm::vec2 base;
						if (!Project(view, frame.pivot + frame.axes[axis] * (frame.length * 0.78f), base)) {
							continue;
						}
						AddLine(vertices, center, base, width, color);

						const glm::vec2 along = tip - base;
						if (glm::length(along) > 1e-3f) {
							const glm::vec2 direction = glm::normalize(along);
							const glm::vec2 side = glm::vec2(-direction.y, direction.x) * (ARROW_WIDTH * ui + grow);
							AddTriangle(vertices, tip + direction * grow, base - direction * grow + side, base - direction * grow - side, color);
						}
					}
					else {
						AddLine(vertices, center, tip, width, color);
						AddSquare(vertices, tip, SQUARE_SIZE * ui + grow, color);
					}
				}

				if (mode != Mode::Rotate) {
					AddSquare(vertices, center, CENTER_SIZE * ui + grow, outline ? OUTLINE_COLOR : colorOf(Handle::Center));
				}
			}
		}
	}
}
