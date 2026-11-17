#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Fox {

	namespace Editor {

		// Interactive handles for moving, rotating and scaling the selected object of a ray tracing scene.
		//
		//   Move:   drag an arrow to move along that world axis, or the center square to move in the view plane
		//   Rotate: drag a ring to rotate about that world axis
		//   Scale:  drag an axis to scale along that axis of the object, or the center square to scale uniformly
		//
		// All screen positions are in pixels of the displayed image, (0, 0) at the top left. The gizmo is
		// drawn as a 2D overlay on top of the rendered image (see BuildOverlay and OverlayRenderer).
		class TransformGizmo {
		public:
			enum class Mode { Translate, Rotate, Scale };

			// The camera the image was rendered with, matching the path tracer's primary rays
			struct View {
				glm::vec3 position;
				glm::vec3 forward;
				glm::vec3 right;
				glm::vec3 up;
				float tanHalfFov = 0.5f; // Vertical
				glm::vec2 size{ 1.0f };  // Pixels
			};

			struct OverlayVertex {
				glm::vec2 position; // Pixels
				glm::vec4 color;    // Straight alpha
			};

			void SetMode(Mode newMode) { mode = newMode; dragging = false; }
			Mode GetMode() const { return mode; }
			static const char* ModeName(Mode mode);

			void Select(int object) { selected = object; dragging = false; hovered = Handle::None; }
			int GetSelected() const { return selected; }

			// Returns true when a handle was grabbed (otherwise the click is free to select another object)
			bool MouseDown(glm::vec2 mouse, const View& view, Fox::Scene::RayTracing::RayTracingScene& scene);

			// Updates the hovered handle, and the object's transform while dragging. snap: round to steps of
			// 0.25 m, 15 degrees or 0.1x
			void MouseMove(glm::vec2 mouse, const View& view, Fox::Scene::RayTracing::RayTracingScene& scene, bool snap);

			void MouseUp() { dragging = false; }
			bool IsDragging() const { return dragging; }

			// Triangles drawing the gizmo of the selected object (nothing without a selection)
			void BuildOverlay(const View& view, const Fox::Scene::RayTracing::RayTracingScene& scene, std::vector<OverlayVertex>& vertices) const;

		private:
			enum class Handle { None = -1, X = 0, Y = 1, Z = 2, Center = 3 };

			struct Frame {
				glm::vec3 pivot;
				glm::vec3 axes[3];   // World axes (move, rotate) or the object's axes (scale)
				float length = 1.0f; // World length of a handle: constant on screen
				float uiScale = 1.0f; // Pixels scale with the image height (1 at 1080 lines)
			};

			Frame FrameOf(const View& view, const Fox::Scene::RayTracing::RayTracingScene& scene) const;
			Handle HandleAt(glm::vec2 mouse, const View& view, const Frame& frame) const;
			std::vector<glm::vec3> RingPoints(const Frame& frame, int axis) const;

			static bool Project(const View& view, const glm::vec3& point, glm::vec2& pixel);
			static void MouseRay(const View& view, glm::vec2 mouse, glm::vec3& origin, glm::vec3& direction);
			static bool AxisParameter(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& point, const glm::vec3& axis, float& parameter);

			Mode mode = Mode::Translate;
			int selected = -1;
			Handle hovered = Handle::None;

			// Drag state
			bool dragging = false;
			Handle active = Handle::None;
			Fox::Scene::RayTracing::RayTracingScene::ObjectTransform start;
			Frame startFrame;
			glm::vec2 startMouse{ 0.0f };
			float startParameter = 0.0f;
			glm::vec3 startPlanePoint{ 0.0f };

			// Rotation: dragging along the ring's direction on screen where it was grabbed turns the object
			glm::vec2 ringTangent{ 1.0f, 0.0f }; // Screen direction of increasing angle
			float pixelsPerRadian = 100.0f;
		};
	}
}
