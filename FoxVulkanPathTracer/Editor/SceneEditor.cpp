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

		void SceneEditor::Draw(Scene& scene, bool* open, float uiScale) {
			if (!ImGui::Begin("Environment", open)) {
				ImGui::End();
				return;
			}

			auto& environment = scene.GetEnvironment();
			bool changed = false;

			changed |= ImGui::Checkbox("Ground", &environment.ground);
			ImGui::SameLine();
			changed |= ImGui::Checkbox("Terrain instead", &environment.terrain);
			ImGui::SetItemTooltip("Sculpt and paint it in the Terrain window (Window menu)");
			changed |= ImGui::DragFloat("Ground height", &environment.groundHeight, 0.01f, -100.0f, 100.0f, "%.2f m");
			changed |= ImGui::ColorEdit3("Ground colour", &environment.groundMaterial.color.x);
			changed |= ImGui::SliderFloat("Ground roughness", &environment.groundMaterial.roughness, 0.0f, 1.0f, "%.2f");

			ImGui::Spacing();
			ImGui::TextWrapped("The sun and other lights are entities with a Light component: select the Sun in the Hierarchy.");
			(void)uiScale;

			if (changed) {
				scene.MarkStructureChanged();
			}
			ImGui::End();
		}

		void SceneEditor::NewScene(Scene& scene, TransformGizmo& gizmo) {
			gizmo.Select({});
			scene.ClearContent();
			path.clear();
			SetStatus("New scene", false);
		}

		void SceneEditor::Open(Scene& scene, TransformGizmo& gizmo, const std::string& file) {
			Scene::CameraPose loaded;
			std::string error;
			if (scene.LoadFromFile(file, loaded, error)) {
				gizmo.Select({});
				path = file;
				SetStatus("Loading " + file + "...", false);
			}
			else {
				SetStatus(error, true);
			}
		}

		void SceneEditor::Save(Scene& scene, const Scene::CameraPose& camera, const std::string& file) {
			const std::string target = file.empty() ? path : file;
			if (target.empty()) {
				RequestSaveAs();
				return;
			}
			std::string error;
			if (scene.SaveToFile(target, camera, error)) {
				path = target;
				SetStatus("Saved " + target, false);
			}
			else {
				SetStatus(error, true);
			}
		}

		void SceneEditor::RequestSaveAs() {
			saveAsOpen = true;
			saveAsFocus = true;
			std::snprintf(saveAsPath, sizeof(saveAsPath), "%s", path.empty() ? "Scenes/scene.json" : path.c_str());
		}

		void SceneEditor::DrawSaveAs(Scene& scene, const Scene::CameraPose& camera, float uiScale) {
			if (!saveAsOpen) {
				return;
			}
			const ImVec2 display = ImGui::GetIO().DisplaySize;
			ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.35f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(480.0f * uiScale, 0.0f), ImGuiCond_Appearing);
			if (ImGui::Begin("Save Scene As", &saveAsOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
				ImGui::TextDisabled("Path from the working directory; Scenes/default.json is what the renderer starts with");
				if (saveAsFocus) {
					ImGui::SetKeyboardFocusHere();
					saveAsFocus = false;
				}
				ImGui::SetNextItemWidth(-1.0f);
				const bool enter = ImGui::InputText("##saveAsPath", saveAsPath, sizeof(saveAsPath), ImGuiInputTextFlags_EnterReturnsTrue);
				if (ImGui::Button("Save") || enter) {
					Save(scene, camera, saveAsPath);
					saveAsOpen = false;
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) {
					saveAsOpen = false;
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
