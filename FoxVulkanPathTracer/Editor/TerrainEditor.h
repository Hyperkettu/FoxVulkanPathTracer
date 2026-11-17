#pragma once

#include <string>
#include <vector>

namespace Fox {

	namespace Editor {

		// The terrain window (edit mode, E key): turns the terrain on in place of the ground plane, sculpts and
		// paints it with a round, soft edged brush (left mouse in the view while the brush is on, B), and sets up
		// its four texture layers and its size.
		class TerrainEditor {
		public:
			using Scene = Fox::Scene::RayTracing::MainRayTracingScene;

			// mouse: display pixels; overUserInterface: the pointer is over an editor window
			void Draw(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse, bool overUserInterface, float uiScale);

			// Clicks in the view go to the brush while it is on and the pointer is over the terrain
			bool IsBrushActive() const { return brushActive; }
			void ToggleBrush() { brushActive = !brushActive; }
			bool MouseDown(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse);
			void MouseUp() { if (stroking) { stroking = false; strokeEnded = true; } }

			// Every frame: applies the brush while the button is held (invert: lower instead of raise and the
			// other way round), and sends what changed to the GPU
			void Update(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse, float deltaTime, bool invert);

			// [ and ]
			void ScaleBrush(float factor) { brush.radius = glm::clamp(brush.radius * factor, 0.25f, 100.0f); }

		private:
			bool brushActive = false;
			bool stroking = false;
			bool strokeEnded = false;
			Fox::Scene::TerrainBrush brush;
			bool hovering = false;
			glm::vec3 hoverPoint{ 0.0f };

			// Grid settings being edited (applied with a rebuild)
			float size = 400.0f;
			int resolution = 513;
			int blendResolution = 1024;

			std::vector<std::string> images;
			std::vector<std::string> folders;
			bool listed = false;

			bool PointUnderMouse(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse, glm::vec3& point) const;
			void DrawBrush(Scene& scene, const TransformGizmo::View& view, float uiScale) const;
			bool DrawLayer(Fox::Scene::TerrainLayer& layer, int index);
		};
	}
}
