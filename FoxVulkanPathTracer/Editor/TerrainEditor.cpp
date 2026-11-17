#include "FoxRenderer.h"

#include <filesystem>

namespace Fox {

	namespace Editor {

		namespace {
			const char* MODE_NAMES[] = { "Raise", "Lower", "Smooth", "Flatten", "Paint" };
			const int RESOLUTIONS[] = { 129, 257, 513, 1025 };
			const int BLEND_RESOLUTIONS[] = { 256, 512, 1024, 2048 };

			// Folders holding a set of ground textures (an albedo.png), with forward slashes
			std::vector<std::string> ListTextureFolders(const std::string& root) {
				std::vector<std::string> result;
				std::error_code error;
				for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error)) {
					if (entry.is_regular_file() && entry.path().filename() == "albedo.png") {
						result.push_back(entry.path().parent_path().generic_string());
					}
				}
				std::sort(result.begin(), result.end());
				return result;
			}

			bool FileCombo(const char* label, std::string& value, const std::vector<std::string>& files) {
				bool changed = false;
				if (ImGui::BeginCombo(label, value.empty() ? "(none)" : value.c_str())) {
					if (ImGui::Selectable("(none)", value.empty())) {
						value.clear();
						changed = true;
					}
					for (const std::string& file : files) {
						if (ImGui::Selectable(file.c_str(), file == value)) {
							value = file;
							changed = true;
						}
					}
					ImGui::EndCombo();
				}
				return changed;
			}

			int IndexOf(const int* values, int count, int value) {
				for (int i = 0; i < count; i++) {
					if (values[i] == value) return i;
				}
				return count - 1;
			}
		}

		bool TerrainEditor::PointUnderMouse(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse, glm::vec3& point) const {
			if (!scene.IsTerrainPlaced()) {
				return false;
			}
			glm::vec3 origin, direction;
			TransformGizmo::MouseRay(view, mouse, origin, direction);
			return scene.GetTerrain().Raycast(origin, direction, point);
		}

		bool TerrainEditor::MouseDown(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse) {
			if (!brushActive) {
				return false;
			}
			glm::vec3 point;
			if (!PointUnderMouse(scene, view, mouse, point)) {
				return false; // Off the terrain: the click selects as usual
			}
			scene.GetTerrain().BeginStroke(point);
			stroking = true;
			hoverPoint = point;
			hovering = true;
			return true;
		}

		void TerrainEditor::Update(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse, float deltaTime, bool invert) {
			hovering = brushActive && PointUnderMouse(scene, view, mouse, hoverPoint);

			if (stroking && hovering) {
				Fox::Scene::TerrainBrush applied = brush;
				if (invert && brush.mode == Fox::Scene::TerrainBrushMode::Raise) applied.mode = Fox::Scene::TerrainBrushMode::Lower;
				else if (invert && brush.mode == Fox::Scene::TerrainBrushMode::Lower) applied.mode = Fox::Scene::TerrainBrushMode::Raise;
				// Long frames (a scene rebuild) would make one huge dab
				scene.GetTerrain().ApplyBrush(applied, hoverPoint, std::min(deltaTime, 0.1f));
			}

			scene.ApplyTerrainEdits(strokeEnded);
			strokeEnded = false;
		}

		void TerrainEditor::DrawBrush(Scene& scene, const TransformGizmo::View& view, float uiScale) const {
			if (!brushActive || !hovering) {
				return;
			}
			const Fox::Scene::Terrain& terrain = scene.GetTerrain();
			ImDrawList* draw = ImGui::GetBackgroundDrawList();

			// The rim and the edge of the hard core, lying on the ground
			auto ring = [&](float radius, ImU32 color, float thickness) {
				const int segments = 64;
				ImVec2 points[segments];
				int count = 0;
				for (int i = 0; i < segments; i++) {
					const float angle = glm::two_pi<float>() * i / segments;
					glm::vec3 p = hoverPoint + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * radius;
					float y;
					if (terrain.HeightAt(p.x, p.z, y)) {
						p.y = y + 0.05f;
					}
					glm::vec2 pixel;
					if (!TransformGizmo::Project(view, p, pixel)) {
						return;
					}
					points[count++] = ImVec2(pixel.x, pixel.y);
				}
				draw->AddPolyline(points, count, color, ImDrawFlags_Closed, thickness);
			};
			const bool paint = brush.mode == Fox::Scene::TerrainBrushMode::Paint;
			const ImU32 color = paint ? IM_COL32(120, 220, 255, 230) : IM_COL32(255, 210, 90, 230);
			ring(brush.radius, color, 2.0f * uiScale);
			if (brush.hardness > 0.05f) {
				ring(brush.radius * brush.hardness, (color & 0x00FFFFFF) | 0x80000000, 1.0f * uiScale);
			}
			glm::vec2 center;
			if (TransformGizmo::Project(view, hoverPoint, center)) {
				draw->AddCircleFilled(ImVec2(center.x, center.y), 3.0f * uiScale, color);
			}
		}

		bool TerrainEditor::DrawLayer(Fox::Scene::TerrainLayer& layer, int index) {
			bool changed = false;
			ImGui::PushID(index);
			char header[96];
			std::snprintf(header, sizeof(header), "Layer %d: %s###layer", index + 1, layer.name.c_str());
			if (ImGui::TreeNode(header)) {
				char name[64];
				std::snprintf(name, sizeof(name), "%s", layer.name.c_str());
				if (ImGui::InputText("Name", name, sizeof(name))) {
					layer.name = name;
				}

				// A whole set of textures at once
				if (ImGui::BeginCombo("Texture set", "(choose a folder)")) {
					for (const std::string& folder : folders) {
						if (ImGui::Selectable(folder.c_str())) {
							Fox::Scene::TerrainLayer loaded = Fox::Scene::TerrainLayer::FromFolder(folder);
							loaded.tiling = layer.tiling;
							loaded.tint = layer.tint;
							loaded.roughnessScale = layer.roughnessScale;
							loaded.normalStrength = layer.normalStrength;
							layer = loaded;
							changed = true;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("Folders with albedo.png, normal.png, roughness.png, metallic.png, ao.png and displacement.png");

				changed |= ImGui::SliderFloat("Tiling", &layer.tiling, 0.25f, 30.0f, "%.2f m per repeat", ImGuiSliderFlags_Logarithmic);
				changed |= ImGui::ColorEdit3("Tint", &layer.tint.x);
				changed |= ImGui::SliderFloat("Roughness", &layer.roughnessScale, 0.0f, 2.0f, "x %.2f");
				changed |= ImGui::SliderFloat("Normal strength", &layer.normalStrength, 0.0f, 3.0f, "%.2f");

				if (ImGui::TreeNode("Files")) {
					changed |= FileCombo("Albedo", layer.albedo, images);
					changed |= FileCombo("Normal", layer.normal, images);
					changed |= ImGui::Checkbox("OpenGL normal map (green up)", &layer.normalGreenUp);
					changed |= FileCombo("Roughness##file", layer.roughness, images);
					changed |= FileCombo("Metallic", layer.metallic, images);
					changed |= FileCombo("Ambient occlusion", layer.occlusion, images);
					changed |= FileCombo("Height", layer.height, images);
					ImGui::SetItemTooltip("Decides which layer shows where layers mix (higher wins)");
					ImGui::TreePop();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
			return changed;
		}

		void TerrainEditor::Draw(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse, bool overUserInterface, float uiScale) {
			if (!listed) {
				images = Fox::Scene::TextureLibrary::ListImages("Textures");
				folders = ListTextureFolders("Textures");
				listed = true;
			}

			ImGui::SetNextWindowPos(ImVec2(470.0f * uiScale, 10.0f * uiScale), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(380.0f * uiScale, 520.0f * uiScale), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Terrain")) {
				ImGui::End();
				return;
			}

			auto& environment = scene.GetEnvironment();
			if (ImGui::Checkbox("Terrain instead of the ground plane", &environment.terrain)) {
				scene.MarkStructureChanged();
				if (!environment.terrain) {
					brushActive = false;
				}
			}
			if (!environment.terrain) {
				ImGui::TextDisabled("The terrain starts flat at the ground height, all grass.");
				ImGui::End();
				return;
			}
			if (!scene.IsTerrainPlaced()) {
				ImGui::TextDisabled("Building...");
				ImGui::End();
				return;
			}

			Fox::Scene::Terrain& terrain = scene.GetTerrain();

			// --- Brush ---
			ImGui::SeparatorText("Brush");
			ImGui::Checkbox("Brush on (B)", &brushActive);
			ImGui::SetItemTooltip("Left mouse on the terrain sculpts or paints; elsewhere clicks select objects as usual");
			int mode = static_cast<int>(brush.mode);
			for (int i = 0; i < IM_ARRAYSIZE(MODE_NAMES); i++) {
				if (i > 0) ImGui::SameLine();
				if (ImGui::RadioButton(MODE_NAMES[i], mode == i)) {
					brush.mode = static_cast<Fox::Scene::TerrainBrushMode>(i);
					brushActive = true;
				}
			}
			if (brush.mode == Fox::Scene::TerrainBrushMode::Paint) {
				for (int i = 0; i < Fox::Scene::TERRAIN_LAYERS; i++) {
					if (i > 0) ImGui::SameLine();
					ImGui::PushID(i);
					const bool chosen = brush.layer == i;
					if (chosen) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
					if (ImGui::Button(terrain.settings.layers[i].name.empty() ? "Layer" : terrain.settings.layers[i].name.c_str())) {
						brush.layer = i;
					}
					if (chosen) ImGui::PopStyleColor();
					ImGui::PopID();
				}
			}
			ImGui::SliderFloat("Radius", &brush.radius, 0.25f, 100.0f, "%.2f m", ImGuiSliderFlags_Logarithmic);
			const bool heightMode = brush.mode == Fox::Scene::TerrainBrushMode::Raise || brush.mode == Fox::Scene::TerrainBrushMode::Lower;
			ImGui::SliderFloat("Strength", &brush.strength, 0.05f, 20.0f, heightMode ? "%.2f m/s" : "%.2f", ImGuiSliderFlags_Logarithmic);
			ImGui::SliderFloat("Hardness", &brush.hardness, 0.0f, 0.95f, "%.2f");
			ImGui::SetItemTooltip("0: the effect fades from the center; higher: full strength further out, then a short soft edge");
			ImGui::TextDisabled("Ctrl: lower / raise instead. [ ]: radius");

			// --- Layers ---
			ImGui::SeparatorText("Layers");
			bool layersChanged = false;
			for (int i = 0; i < Fox::Scene::TERRAIN_LAYERS; i++) {
				layersChanged |= DrawLayer(terrain.settings.layers[i], i);
			}
			layersChanged |= ImGui::SliderFloat("Blend softness", &terrain.settings.blendSoftness, 0.01f, 1.0f, "%.2f");
			ImGui::SetItemTooltip("Where layers mix, the higher of their height maps shows: small values give crisp edges along the shapes");
			if (layersChanged) {
				scene.ApplyTerrainLayers();
			}
			if (ImGui::SmallButton("Rescan textures")) {
				listed = false;
			}

			// --- Grid ---
			ImGui::SeparatorText("Size");
			if (ImGui::IsWindowAppearing()) {
				size = terrain.settings.size;
				resolution = terrain.settings.resolution;
				blendResolution = terrain.settings.blendResolution;
			}
			ImGui::DragFloat("Size", &size, 1.0f, 10.0f, 4000.0f, "%.0f m");
			int resolutionIndex = IndexOf(RESOLUTIONS, IM_ARRAYSIZE(RESOLUTIONS), resolution);
			if (ImGui::Combo("Heights", &resolutionIndex, "129 x 129\0" "257 x 257\0" "513 x 513\0" "1025 x 1025\0")) {
				resolution = RESOLUTIONS[resolutionIndex];
			}
			int blendIndex = IndexOf(BLEND_RESOLUTIONS, IM_ARRAYSIZE(BLEND_RESOLUTIONS), blendResolution);
			if (ImGui::Combo("Blend map", &blendIndex, "256 x 256\0" "512 x 512\0" "1024 x 1024\0" "2048 x 2048\0")) {
				blendResolution = BLEND_RESOLUTIONS[blendIndex];
			}
			ImGui::TextDisabled("%.2f m between heights, %.2f m per blend texel", size / (resolution - 1), size / blendResolution);
			const bool sizeChanged = size != terrain.settings.size || resolution != terrain.settings.resolution
				|| blendResolution != terrain.settings.blendResolution;
			ImGui::BeginDisabled(!sizeChanged);
			if (ImGui::Button("Apply size")) {
				terrain.settings.size = size;
				terrain.Resize(resolution, blendResolution);
				scene.MarkStructureChanged();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Reset flat")) {
				Fox::Scene::TerrainSettings settings = terrain.settings;
				terrain.Create(settings);
				scene.MarkStructureChanged();
			}
			ImGui::SetItemTooltip("Flat at its base height, the first layer everywhere");

			glm::vec3 position = terrain.settings.position;
			ImGui::DragFloat3("Center", &position.x, 0.05f, 0.0f, 0.0f, "%.2f");
			if (ImGui::IsItemEdited()) {
				terrain.settings.position = position;
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				scene.MarkStructureChanged();
			}

			ImGui::End();

			if (!overUserInterface) {
				DrawBrush(scene, view, uiScale);
			}
		}
	}
}
