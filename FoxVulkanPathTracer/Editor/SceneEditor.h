#pragma once

#include <string>
#include <vector>

namespace Fox {

	namespace Editor {

		// Scene files and the environment (edit mode, E key):
		//   - new, open, save and save as, run from the File menu (and the content browser, which opens scenes)
		//   - the Environment window: the ground plane or terrain (lights are entities with a Light component)
		// The status of the last of these (saved, loading, errors, rebuilt) shows in the status bar.
		class SceneEditor {
		public:
			using Scene = Fox::Scene::RayTracing::MainRayTracingScene;

			// The Environment window
			void Draw(Scene& scene, bool* open, float uiScale);

			// --- Files ---

			void NewScene(Scene& scene, TransformGizmo& gizmo);
			void Open(Scene& scene, TransformGizmo& gizmo, const std::string& file);
			// Saves to file, or to the scene's own file when empty
			void Save(Scene& scene, const Scene::CameraPose& camera, const std::string& file = {});
			// The file the scene came from or was last saved to (empty for a new scene)
			const std::string& GetPath() const { return path; }

			// Save As asks for a path in a small window of its own
			void RequestSaveAs();
			void DrawSaveAs(Scene& scene, const Scene::CameraPose& camera, float uiScale);

			// After the renderer rebuilt the scene (or failed to load part of it)
			void Rebuilt(bool succeeded, const std::string& error, float seconds);

			const std::string& GetStatus(bool& isError) const {
				isError = statusIsError;
				return status;
			}
			void SetStatus(const std::string& text, bool isError) {
				status = text;
				statusIsError = isError;
			}

			// glTF files under folder, with forward slashes, sorted
			static std::vector<std::string> ListModels(const std::string& folder);

			// Sphere materials to start from
			static constexpr int SPHERE_PRESET_COUNT = 6;
			static const char* const SPHERE_PRESETS[SPHERE_PRESET_COUNT];
			static Fox::Scene::SurfaceMaterial SpherePreset(int preset);

		private:
			std::string path = Scene::DEFAULT_SCENE_FILE;
			std::string status;
			bool statusIsError = false;

			bool saveAsOpen = false;
			bool saveAsFocus = false;
			char saveAsPath[260] = {};
		};
	}
}
