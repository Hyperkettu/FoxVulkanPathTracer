#include "FoxRenderer.h"

#include <filesystem>
#include <cctype>

namespace Fox {

	namespace Editor {

		namespace {
			std::string Lower(std::string text) {
				std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return text;
			}

			std::string SizeText(uintmax_t bytes) {
				char text[32];
				if (bytes >= 1024ull * 1024ull) {
					std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
				}
				else {
					std::snprintf(text, sizeof(text), "%.0f KB", std::ceil(static_cast<double>(bytes) / 1024.0));
				}
				return text;
			}
		}

		ContentBrowser::Kind ContentBrowser::KindOf(const std::string& path, bool directory) {
			if (directory) {
				return Kind::Folder;
			}
			const std::string extension = Lower(std::filesystem::path(path).extension().string());
			if (extension == ".glb" || extension == ".gltf") {
				return Kind::Model;
			}
			if (extension == ".json") {
				return Kind::Scene;
			}
			if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp"
				|| extension == ".hdr" || extension == ".exr") {
				return Kind::Image;
			}
			return Kind::Other;
		}

		const char* ContentBrowser::KindTag(Kind kind) {
			switch (kind) {
			case Kind::Folder: return "[folder]";
			case Kind::Model: return "[model]";
			case Kind::Scene: return "[scene]";
			case Kind::Image: return "[image]";
			default: return "";
			}
		}

		void ContentBrowser::ShowFolder(const std::string& newFolder) {
			folder = newFolder;
			scanned = false;
			filter[0] = '\0';
		}

		void ContentBrowser::Scan() {
			entries.clear();
			std::error_code error;
			for (const auto& item : std::filesystem::directory_iterator(folder, error)) {
				Entry entry;
				entry.path = item.path().generic_string();
				entry.name = item.path().filename().string();
				const bool directory = item.is_directory(error);
				entry.kind = KindOf(entry.path, directory);
				entry.size = directory ? 0 : item.file_size(error);
				entries.push_back(std::move(entry));
			}
			// Folders first, then by name
			std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
				if ((a.kind == Kind::Folder) != (b.kind == Kind::Folder)) {
					return a.kind == Kind::Folder;
				}
				return Lower(a.name) < Lower(b.name);
			});
			scanned = true;
		}

		void ContentBrowser::DrawFolderTree(const std::string& path, int depth) {
			std::vector<std::string> children;
			std::error_code error;
			for (const auto& item : std::filesystem::directory_iterator(path, error)) {
				if (item.is_directory(error)) {
					children.push_back(item.path().generic_string());
				}
			}
			std::sort(children.begin(), children.end());

			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
			if (children.empty()) {
				flags |= ImGuiTreeNodeFlags_Leaf;
			}
			if (path == folder) {
				flags |= ImGuiTreeNodeFlags_Selected;
			}
			// The folder shown stays visible: its parents open
			if (folder.compare(0, path.size(), path) == 0 && folder.size() > path.size()) {
				ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
			}
			const std::string label = depth == 0 ? path : std::filesystem::path(path).filename().string();
			const bool open = ImGui::TreeNodeEx(path.c_str(), flags, "%s", label.c_str());
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
				ShowFolder(path);
			}
			if (open) {
				for (const std::string& child : children) {
					DrawFolderTree(child, depth + 1);
				}
				ImGui::TreePop();
			}
		}

		void ContentBrowser::Activate(const Entry& entry, const Actions& actions) {
			switch (entry.kind) {
			case Kind::Folder:
				ShowFolder(entry.path);
				break;
			case Kind::Model:
				if (actions.addModel) {
					actions.addModel(entry.path, displacement);
				}
				break;
			case Kind::Scene:
				if (actions.openScene) {
					actions.openScene(entry.path);
				}
				break;
			default:
				break;
			}
		}

		void ContentBrowser::Draw(bool* open, const Actions& actions, float uiScale) {
			if (!ImGui::Begin("Content Browser", open)) {
				ImGui::End();
				return;
			}
			if (!scanned) {
				Scan();
			}

			// Toolbar: up, where, filter, rescan, displacement for new models
			const std::filesystem::path current(folder);
			const bool atRoot = std::find(roots.begin(), roots.end(), folder) != roots.end();
			ImGui::BeginDisabled(atRoot);
			if (ImGui::Button("Up")) {
				ShowFolder(current.parent_path().generic_string());
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Rescan")) {
				scanned = false;
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(folder.c_str());
			ImGui::SameLine();
			ImGui::SetNextItemWidth(220.0f * uiScale);
			ImGui::InputTextWithHint("##filter", "Filter", filter, sizeof(filter));
			ImGui::SameLine();
			ImGui::Checkbox("Models with displacement", &displacement);
			ImGui::SetItemTooltip("Relief reconstructed from a model's normal maps when it is added: much more geometry, slower to load");

			if (ImGui::BeginTable("##browser", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
				ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 200.0f * uiScale);
				ImGui::TableSetupColumn("Files", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();

				// Folders
				ImGui::TableSetColumnIndex(0);
				ImGui::BeginChild("##folders");
				std::error_code error;
				for (const std::string& root : roots) {
					if (std::filesystem::is_directory(root, error)) {
						DrawFolderTree(root, 0);
					}
					else {
						ImGui::TextDisabled("%s (missing)", root.c_str());
					}
				}
				ImGui::EndChild();

				// Files of the folder
				ImGui::TableSetColumnIndex(1);
				ImGui::BeginChild("##files");
				const std::string wanted = Lower(filter);
				const Entry* activated = nullptr;
				if (ImGui::BeginTable("##entries", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
					ImGui::TableSetupScrollFreeze(0, 1);
					ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Kind");
					ImGui::TableSetupColumn("Size");
					ImGui::TableHeadersRow();

					int shown = 0;
					for (const Entry& entry : entries) {
						if (!wanted.empty() && Lower(entry.name).find(wanted) == std::string::npos) {
							continue;
						}
						shown++;
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::PushID(entry.path.c_str());
						if (entry.kind == Kind::Other || entry.kind == Kind::Image) {
							ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(entry.kind == Kind::Image ? ImGuiCol_Text : ImGuiCol_TextDisabled));
						}
						if (ImGui::Selectable(entry.name.c_str(), selected == entry.path,
							ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
							selected = entry.path;
							if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
								activated = &entry;
							}
						}
						if (entry.kind == Kind::Other || entry.kind == Kind::Image) {
							ImGui::PopStyleColor();
						}
						if (ImGui::IsItemHovered() && entry.kind != Kind::Folder) {
							ImGui::SetItemTooltip("%s\n%s", entry.path.c_str(),
								entry.kind == Kind::Model ? "Double-click to add it to the scene, or drag it onto an entity in the Hierarchy"
								: entry.kind == Kind::Scene ? "Double-click to open this scene" : "");
						}

						// Models drag into the Hierarchy
						if (entry.kind == Kind::Model && ImGui::BeginDragDropSource()) {
							ModelPayload payload{};
							std::snprintf(payload.file, sizeof(payload.file), "%s", entry.path.c_str());
							payload.displacement = displacement;
							ImGui::SetDragDropPayload(MODEL_PAYLOAD, &payload, sizeof(payload));
							ImGui::Text("Add %s", entry.name.c_str());
							ImGui::TextDisabled("Drop onto an entity in the Hierarchy (or its empty space for a root)");
							ImGui::EndDragDropSource();
						}

						if (ImGui::BeginPopupContextItem("##entryMenu")) {
							selected = entry.path;
							if (entry.kind == Kind::Model && ImGui::MenuItem("Add to Scene")) {
								activated = &entry;
							}
							if (entry.kind == Kind::Scene && ImGui::MenuItem("Open Scene")) {
								activated = &entry;
							}
							if (entry.kind == Kind::Folder && ImGui::MenuItem("Open Folder")) {
								activated = &entry;
							}
							if (ImGui::MenuItem("Copy Path")) {
								ImGui::SetClipboardText(entry.path.c_str());
							}
							ImGui::EndPopup();
						}
						ImGui::PopID();

						ImGui::TableSetColumnIndex(1);
						ImGui::TextDisabled("%s", KindTag(entry.kind));
						ImGui::TableSetColumnIndex(2);
						if (entry.kind != Kind::Folder) {
							ImGui::TextDisabled("%s", SizeText(entry.size).c_str());
						}
					}
					ImGui::EndTable();
					if (shown == 0) {
						ImGui::TextDisabled(entries.empty() ? "Empty folder" : "Nothing matches the filter");
					}
				}
				ImGui::EndChild();
				ImGui::EndTable();

				if (activated) {
					const Entry entry = *activated; // Activating a folder rescans the entries
					Activate(entry, actions);
				}
			}

			ImGui::End();
		}
	}
}
