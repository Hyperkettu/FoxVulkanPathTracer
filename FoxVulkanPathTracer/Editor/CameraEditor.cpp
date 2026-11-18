#include "FoxRenderer.h"

namespace Fox {

	namespace Editor {

		namespace {
			Fox::Scene::CameraKey KeyFromView(Fox::Graphics::Vulkan::Camera& camera) {
				Fox::Scene::CameraKey key;
				key.position = camera.GetPosition();
				key.target = camera.GetPosition() + camera.GetForward() * 10.0f; // Something 10 m ahead
				return key;
			}
		}

		void CameraEditor::AddKeyFromView(Fox::Scene::CameraManager& manager, Fox::Graphics::Vulkan::Camera& camera) {
			Fox::Scene::CameraPath* path = manager.ActivePath();
			if (!path) {
				path = &manager.AddPath();
				selectedKey = -1;
			}
			manager.Stop();
			auto& keys = path->keys;
			const int at = (selectedKey >= 0 && selectedKey < static_cast<int>(keys.size())) ? selectedKey + 1 : static_cast<int>(keys.size());
			keys.insert(keys.begin() + at, KeyFromView(camera));
			selectedKey = at;
		}

		bool CameraEditor::Draw(Fox::Scene::CameraManager& manager, Fox::Graphics::Vulkan::Camera& camera,
			const TransformGizmo::View& view, bool* open, float uiScale) {
			bool moved = false;

			if (!ImGui::Begin("Camera", open)) {
				ImGui::End();
				return false;
			}

			ImGui::SliderFloat("Flight speed", &manager.flySpeed, 0.5f, 100.0f, "%.1f m/s", ImGuiSliderFlags_Logarithmic);
			ImGui::SetItemTooltip("W A S D R F outside a path's flight");

			// --- Paths ---
			ImGui::SeparatorText("Paths");
			auto& paths = manager.GetPaths();
			const int active = manager.GetActivePath();
			if (ImGui::BeginCombo("Path", active >= 0 ? paths[active].name.c_str() : "(none)")) {
				for (int i = 0; i < static_cast<int>(paths.size()); i++) {
					ImGui::PushID(i);
					if (ImGui::Selectable(paths[i].name.c_str(), i == active)) {
						manager.SetActivePath(i);
						selectedKey = -1;
					}
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			if (ImGui::Button("New path")) {
				manager.AddPath();
				selectedKey = -1;
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(active < 0);
			if (ImGui::Button("Remove path")) {
				manager.RemovePath(active);
				selectedKey = -1;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::Checkbox("Show in view", &showPath);

			Fox::Scene::CameraPath* path = manager.ActivePath();
			if (!path) {
				ImGui::TextDisabled("No path: New path, or K adds a key where the camera is.");
				ImGui::End();
				return false;
			}

			char name[128];
			std::snprintf(name, sizeof(name), "%s", path->name.c_str());
			if (ImGui::InputText("Name", name, sizeof(name))) {
				path->name = name;
			}
			ImGui::SliderFloat("Speed", &path->speed, 0.1f, 50.0f, "%.1f m/s", ImGuiSliderFlags_Logarithmic);
			ImGui::SliderFloat("Ease in/out", &path->ease, 0.0f, 5.0f, "%.1f s");
			ImGui::SetItemTooltip("Seconds to speed up from rest at the start and to slow down to rest at the end");
			ImGui::Checkbox("Loop", &path->loop);
			ImGui::SetItemTooltip("The last key joins the first, and the flight goes round until stopped");
			ImGui::SameLine();
			ImGui::Checkbox("Look along the path", &path->lookAlongPath);
			ImGui::SetItemTooltip("Look where the path goes, instead of where each key looks");

			// --- Playback ---
			const float duration = path->Duration();
			ImGui::Text("%d keys, %.1f m, %.1f s", static_cast<int>(path->keys.size()), path->Length(), duration);
			ImGui::BeginDisabled(!path->IsFlyable());
			if (ImGui::Button(manager.IsPlaying() ? "Stop (P)" : "Play (P)", ImVec2(90.0f * uiScale, 0.0f))) {
				manager.TogglePlay();
			}
			ImGui::SameLine();
			float time = std::min(manager.GetTime(), duration);
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::SliderFloat("##time", &time, 0.0f, std::max(duration, 0.01f), "%.2f s")) {
				manager.Stop();
				manager.SetTime(time);
				manager.ApplyPose(time, camera);
				moved = true;
			}
			ImGui::EndDisabled();

			// --- Keys ---
			ImGui::SeparatorText("Keys");
			auto& keys = path->keys;
			selectedKey = std::min(selectedKey, static_cast<int>(keys.size()) - 1);
			if (ImGui::BeginListBox("##keys", ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * 6.0f))) {
				for (int i = 0; i < static_cast<int>(keys.size()); i++) {
					char label[96];
					std::snprintf(label, sizeof(label), "Key %d   (%.1f, %.1f, %.1f)", i + 1,
						keys[i].position.x, keys[i].position.y, keys[i].position.z);
					ImGui::PushID(i);
					if (ImGui::Selectable(label, i == selectedKey)) {
						selectedKey = i;
					}
					// Double click: look from there
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						manager.Stop();
						camera.SetPosition(keys[i].position);
						camera.SetCameraTarget(keys[i].target);
						moved = true;
					}
					ImGui::PopID();
				}
				ImGui::EndListBox();
			}

			if (ImGui::Button("Add key from view (K)")) {
				AddKeyFromView(manager, camera);
			}
			ImGui::SetItemTooltip("Where the camera is and where it looks, after the selected key");

			const bool hasSelection = selectedKey >= 0 && selectedKey < static_cast<int>(keys.size());
			ImGui::BeginDisabled(!hasSelection);
			if (ImGui::Button("Set to view") && hasSelection) {
				keys[selectedKey] = KeyFromView(camera);
			}
			ImGui::SameLine();
			if (ImGui::Button("Go to") && hasSelection) {
				manager.Stop();
				camera.SetPosition(keys[selectedKey].position);
				camera.SetCameraTarget(keys[selectedKey].target);
				moved = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Up") && hasSelection && selectedKey > 0) {
				std::swap(keys[selectedKey], keys[selectedKey - 1]);
				selectedKey--;
			}
			ImGui::SameLine();
			if (ImGui::Button("Down") && hasSelection && selectedKey + 1 < static_cast<int>(keys.size())) {
				std::swap(keys[selectedKey], keys[selectedKey + 1]);
				selectedKey++;
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove") && hasSelection) {
				keys.erase(keys.begin() + selectedKey);
				selectedKey = std::min(selectedKey, static_cast<int>(keys.size()) - 1);
				manager.Stop();
			}
			if (hasSelection && selectedKey < static_cast<int>(keys.size())) {
				ImGui::DragFloat3("Position", &keys[selectedKey].position.x, 0.05f, 0.0f, 0.0f, "%.2f");
				ImGui::DragFloat3("Looks at", &keys[selectedKey].target.x, 0.05f, 0.0f, 0.0f, "%.2f");
			}
			ImGui::EndDisabled();

			ImGui::End();

			// While the camera flies the path it is on it: nothing to see
			if (showPath && !manager.IsPlaying()) {
				DrawPath(*path, view, uiScale);
			}
			return moved;
		}

		void CameraEditor::DrawPath(const Fox::Scene::CameraPath& path, const TransformGizmo::View& view, float uiScale) const {
			ImDrawList* draw = ImGui::GetBackgroundDrawList(); // Over the image, under the windows
			const ImU32 lineColor = IM_COL32(255, 200, 60, 220);
			const ImU32 keyColor = IM_COL32(255, 255, 255, 230);
			const ImU32 selectedColor = IM_COL32(80, 180, 255, 255);
			const float thickness = 2.0f * uiScale;

			// The spline: pieces between points both in front of the camera
			const std::vector<glm::vec3> points = path.Polyline(24);
			glm::vec2 previous;
			bool previousVisible = false;
			for (const glm::vec3& point : points) {
				glm::vec2 pixel;
				const bool visible = TransformGizmo::Project(view, point, pixel);
				if (visible && previousVisible) {
					draw->AddLine(ImVec2(previous.x, previous.y), ImVec2(pixel.x, pixel.y), lineColor, thickness);
				}
				previous = pixel;
				previousVisible = visible;
			}

			// Keys, each with a short line where it looks
			for (int i = 0; i < static_cast<int>(path.keys.size()); i++) {
				const auto& key = path.keys[i];
				glm::vec2 at, look;
				if (!TransformGizmo::Project(view, key.position, at)) {
					continue;
				}
				const ImU32 color = i == selectedKey ? selectedColor : keyColor;
				glm::vec3 direction = key.target - key.position;
				if (glm::dot(direction, direction) > 1e-8f && !path.lookAlongPath) {
					direction = glm::normalize(direction);
					if (TransformGizmo::Project(view, key.position + direction * 1.5f, look)) {
						draw->AddLine(ImVec2(at.x, at.y), ImVec2(look.x, look.y), color, thickness);
					}
				}
				draw->AddCircleFilled(ImVec2(at.x, at.y), 5.0f * uiScale, color);
				char label[16];
				std::snprintf(label, sizeof(label), "%d", i + 1);
				draw->AddText(ImVec2(at.x + 7.0f * uiScale, at.y - 16.0f * uiScale), color, label);
			}
		}
	}
}
