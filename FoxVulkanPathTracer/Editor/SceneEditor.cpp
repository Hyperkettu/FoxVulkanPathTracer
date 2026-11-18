#include "FoxRenderer.h"

#include <filesystem>

namespace Fox {

	namespace Editor {

		const char* const SceneEditor::SPHERE_PRESETS[SceneEditor::SPHERE_PRESET_COUNT] = { "Glass", "Frosted glass", "Chrome", "Gold", "White plastic" };

		std::vector<std::string> SceneEditor::ListModels(const std::string& folder) {
			std::vector<std::string> files;
			std::error_code error;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, error)) {
				if (!entry.is_regular_file()) {
					continue;
				}
				std::string extension = entry.path().extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (extension == ".glb" || extension == ".gltf") {
					files.push_back(entry.path().generic_string());
				}
			}
			std::sort(files.begin(), files.end());
			return files;
		}

		Fox::Scene::SurfaceMaterial SceneEditor::SpherePreset(int preset) {
			Scene::SurfaceMaterial material;
			switch (preset) {
			case 0: // Clear glass with the faint green of thick window glass
				material.color = glm::vec3(1.0f);
				material.roughness = 0.0f;
				material.transmission = 1.0f;
				material.ior = 1.5f;
				material.attenuationColor = glm::vec3(0.80f, 0.94f, 0.90f);
				material.attenuationDistance = 2.0f;
				break;
			case 1:
				material.color = glm::vec3(1.0f);
				material.roughness = 0.25f;
				material.transmission = 1.0f;
				material.ior = 1.5f;
				break;
			case 2:
				material.color = glm::vec3(0.95f, 0.94f, 0.92f);
				material.roughness = 0.05f;
				material.metallic = 1.0f;
				break;
			case 3:
				material.color = glm::vec3(1.0f, 0.78f, 0.34f);
				material.roughness = 0.2f;
				material.metallic = 1.0f;
				break;
			default:
				material.color = glm::vec3(0.85f);
				material.roughness = 0.4f;
				break;
			}
			return material;
		}

		void SceneEditor::Draw(Scene& scene, TransformGizmo& gizmo, const Scene::CameraPose& camera, float uiScale) {
			ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 400.0f * uiScale - 10.0f * uiScale, 10.0f * uiScale), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(400.0f * uiScale, 560.0f * uiScale), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Scene")) {
				ImGui::End();
				return;
			}

			// --- File ---
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("##path", path, sizeof(path));
			if (ImGui::Button("Save")) {
				std::string error;
				if (scene.SaveToFile(path, camera, error)) {
					status = std::string("Saved ") + path;
					statusIsError = false;
				}
				else {
					status = error;
					statusIsError = true;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Load")) {
				gizmo.Select({});
				Scene::CameraPose loaded;
				std::string error;
				if (scene.LoadFromFile(path, loaded, error)) {
					status = std::string("Loading ") + path + "...";
					statusIsError = false;
				}
				else {
					status = error;
					statusIsError = true;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("New")) {
				gizmo.Select({});
				scene.ClearContent();
				status = "New scene";
				statusIsError = false;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(the scene starts from %s)", Scene::DEFAULT_SCENE_FILE);

			if (!status.empty()) {
				if (statusIsError) {
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", status.c_str());
				}
				else {
					ImGui::TextDisabled("%s", status.c_str());
				}
			}

			// New objects stand on the ground in front of the camera
			glm::vec3 forward = camera.target - camera.position;
			forward.y = 0.0f;
			forward = glm::dot(forward, forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(1.0f, 0.0f, 0.0f);
			glm::vec3 inFront = camera.position + forward * 12.0f;
			inFront.y = scene.GetGroundHeightAt(inFront.x, inFront.z);

			// --- Add ---
			if (ImGui::CollapsingHeader("Add", ImGuiTreeNodeFlags_DefaultOpen)) {
				if (!modelsListed) {
					modelFiles = ListModels("Models");
					modelsListed = true;
				}
				modelChoice = std::clamp(modelChoice, 0, std::max(static_cast<int>(modelFiles.size()) - 1, 0));
				const char* preview = modelFiles.empty() ? "(no models in Models/)" : modelFiles[modelChoice].c_str();
				if (ImGui::BeginCombo("Model", preview)) {
					for (int i = 0; i < static_cast<int>(modelFiles.size()); i++) {
						if (ImGui::Selectable(modelFiles[i].c_str(), i == modelChoice)) {
							modelChoice = i;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::Checkbox("Displacement from normal maps", &displacement);
				ImGui::SetItemTooltip("Real relief reconstructed from the model's normal maps: much more geometry, slower to load");
				ImGui::BeginDisabled(modelFiles.empty());
				if (ImGui::Button("Add model") && !modelFiles.empty()) {
					glm::vec3 position = inFront;
					// Models carry their own height above the ground plane (e.g. the cabin's foundation); on
					// terrain they follow it
					position.y = inFront.y - scene.GetEnvironment().groundHeight;
					gizmo.Select(scene.AddModel(modelFiles[modelChoice], displacement, position));
					status = "Loading " + modelFiles[modelChoice] + "...";
					statusIsError = false;
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::SmallButton("Rescan Models folder")) {
					modelsListed = false;
				}

				ImGui::Spacing();
				ImGui::Combo("Sphere", &spherePreset, SPHERE_PRESETS, SPHERE_PRESET_COUNT);
				ImGui::SliderFloat("Radius (m)", &sphereRadius, 0.1f, 5.0f, "%.2f");
				if (ImGui::Button("Add sphere")) {
					gizmo.Select(scene.AddSphereObject(inFront + glm::vec3(0.0f, sphereRadius, 0.0f), sphereRadius, SpherePreset(spherePreset)));
				}
			}

			ImGui::TextDisabled("The scene's entities: the Hierarchy window; their components: the Inspector");

			// --- Environment ---
			if (ImGui::CollapsingHeader("Environment")) {
				auto& environment = scene.GetEnvironment();
				bool changed = false;

				changed |= ImGui::Checkbox("Ground", &environment.ground);
				ImGui::SameLine();
				changed |= ImGui::Checkbox("Terrain instead (see the Terrain window)", &environment.terrain);
				changed |= ImGui::DragFloat("Ground height", &environment.groundHeight, 0.01f, -100.0f, 100.0f, "%.2f m");
				changed |= ImGui::ColorEdit3("Ground colour", &environment.groundMaterial.color.x);
				changed |= ImGui::SliderFloat("Ground roughness", &environment.groundMaterial.roughness, 0.0f, 1.0f, "%.2f");

				ImGui::Spacing();
				changed |= ImGui::Checkbox("Sun", &environment.sun);
				const glm::vec3 direction = glm::normalize(environment.sunDirection);
				float azimuth = glm::degrees(std::atan2(direction.z, direction.x));
				float elevation = glm::degrees(std::asin(glm::clamp(direction.y, -1.0f, 1.0f)));
				bool turned = ImGui::SliderFloat("Sun direction", &azimuth, -180.0f, 180.0f, "%.0f deg");
				turned |= ImGui::SliderFloat("Sun elevation", &elevation, -5.0f, 90.0f, "%.1f deg");
				if (turned) {
					const float a = glm::radians(azimuth), e = glm::radians(elevation);
					environment.sunDirection = glm::vec3(std::cos(e) * std::cos(a), std::sin(e), std::cos(e) * std::sin(a));
					changed = true;
				}
				changed |= ImGui::ColorEdit3("Sun colour", &environment.sunColor.x);
				changed |= ImGui::SliderFloat("Sun strength", &environment.sunIntensity, 0.0f, 100.0f, "%.1f");
				changed |= ImGui::SliderFloat("Sun size", &environment.sunAngularRadius, 0.1f, 10.0f, "%.2f deg");

				if (changed) {
					scene.MarkStructureChanged();
				}
			}

			ImGui::End();
		}

		void SceneEditor::Rebuilt(bool succeeded, const std::string& error, float seconds) {
			if (!succeeded) {
				status = error;
				statusIsError = true;
				return;
			}
			char text[96];
			std::snprintf(text, sizeof(text), "Scene rebuilt in %.2f s", seconds);
			status = text;
			statusIsError = false;
		}
	}
}
