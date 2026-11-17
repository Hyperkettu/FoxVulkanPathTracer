#pragma once

#include <string>
#include <vector>

namespace Fox {

	namespace Editor {

		// The scene window (edit mode, E key): save and load scene files, add models (any glTF file under
		// Models/) and spheres, list and remove objects, and the environment (ground, sun). Adding or
		// removing objects rebuilds the scene; the renderer does that between frames.
		class SceneEditor {
		public:
			void Draw(Fox::Scene::RayTracing::MainRayTracingScene& scene, TransformGizmo& gizmo,
				const Fox::Scene::RayTracing::MainRayTracingScene::CameraPose& camera, float uiScale);

			// After the renderer rebuilt the scene (or failed to load part of it)
			void Rebuilt(bool succeeded, const std::string& error, float seconds);

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
