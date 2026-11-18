#pragma once

#include <string>
#include <vector>

namespace Fox {

	namespace Editor {

		// The scene window (edit mode, E key): save and load scene files, add models (any glTF file under
		// Models/) and spheres, and the environment (ground, sun). The entities themselves are in the Hierarchy
		// and Inspector windows (SceneGraphEditor). Adding or removing meshes rebuilds the scene; the renderer
		// does that between frames.
		class SceneEditor {
		public:
			void Draw(Fox::Scene::RayTracing::MainRayTracingScene& scene, TransformGizmo& gizmo,
				const Fox::Scene::RayTracing::MainRayTracingScene::CameraPose& camera, float uiScale);

			// After the renderer rebuilt the scene (or failed to load part of it)
			void Rebuilt(bool succeeded, const std::string& error, float seconds);

			// glTF files under folder, with forward slashes, sorted
			static std::vector<std::string> ListModels(const std::string& folder);

			// Sphere materials to start from
			static constexpr int SPHERE_PRESET_COUNT = 5;
			static const char* const SPHERE_PRESETS[SPHERE_PRESET_COUNT];
			static Fox::Scene::SurfaceMaterial SpherePreset(int preset);

		private:
			using Scene = Fox::Scene::RayTracing::MainRayTracingScene;

			char path[260] = "Scenes/default.json";
			std::string status;
			bool statusIsError = false;

			std::vector<std::string> modelFiles;
			bool modelsListed = false;
			int modelChoice = 0;
			bool displacement = false;

			float sphereRadius = 1.0f;
			int spherePreset = 0;
		};
	}
}
