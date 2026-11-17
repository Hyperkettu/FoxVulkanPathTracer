#pragma once

#include <string>
#include <vector>

namespace Fox {

	namespace Editor {

		// The tree editor window (edit mode, E key): add, duplicate and remove trees, and change everything
		// about each: shape, randomness, the trunk's cross-section and curve (spline editors), leaves, its
		// own textures and its wind. Changes are applied by the renderer through ApplyTreeChanges once no
		// control is being dragged (or right away with live update).
		class TreeEditor {
		public:
			// Draws the window and makes the changes in the scene. cameraPosition / cameraForward place new trees.
			void Draw(Fox::Scene::RayTracing::MainRayTracingScene& scene, TransformGizmo& gizmo,
				const glm::vec3& cameraPosition, const glm::vec3& cameraForward, float uiScale);

			// Changes wait until no control is being dragged, unless live update is on
			bool LiveUpdate() const { return liveUpdate; }

			// After the renderer applied changes (or failed to)
			void Applied(Fox::Scene::RayTracing::MainRayTracingScene& scene, TransformGizmo& gizmo, bool succeeded, const std::string& error, float seconds);

			int GetSelected() const { return selected; }

		private:
			using Scene = Fox::Scene::RayTracing::MainRayTracingScene;

			void DrawTreeList(Scene& scene, TransformGizmo& gizmo, const glm::vec3& cameraPosition, const glm::vec3& cameraForward);
			void DrawPlacement(Scene& scene, Scene::Tree& tree);
			bool DrawCrossSection(Fox::Graphics::Geometry::TreeSettings& shape, float uiScale);
			bool DrawTrunkCurve(Fox::Graphics::Geometry::TreeSettings& shape, float uiScale);
			bool DrawTextures(const char* id, Scene::SurfaceTextures& textures, bool& geometryChanged);
			bool TextureCombo(const char* label, std::string& file);

			int selected = 0;
			bool liveUpdate = false;
			std::string status;
			bool statusIsError = false;

			// Spline editing
			int sectionPoint = -1;  // Control point being dragged / last dragged
			int curvePoint = -1;
			int curveAxis = 0;      // 0: x (front view), 1: z (side view)

			std::vector<std::string> images; // Texture files to choose from
			bool imagesListed = false;
		};
	}
}
