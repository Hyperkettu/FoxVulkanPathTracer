#include "FoxRenderer.h"
#include "imgui_internal.h"

#include <cctype>

namespace Fox {

	namespace Editor {

		namespace {
			std::string Lower(std::string text) {
				std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return text;
			}

			// "Create/Light/Point Light" -> "Create > Light > Point Light"
			std::string Readable(const std::string& path) {
				std::string result;
				for (char c : path) {
					if (c == '/') {
						result += "  >  ";
					}
					else {
						result += c;
					}
				}
				return result;
			}

			bool IsSeparator(const std::string& path) {
				return path.size() >= 2 && path.compare(path.size() - 2, 2, "/-") == 0;
			}
		}

		const char* EditorShell::WindowTitle(Window window) {
			switch (window) {
			case Window::Hierarchy: return "Hierarchy";
			case Window::Inspector: return "Inspector";
			case Window::ContentBrowser: return "Content Browser";
			case Window::Environment: return "Environment";
			case Window::Trees: return "Trees";
			case Window::Terrain: return "Terrain";
			case Window::MeshPaint: return "Mesh Paint";
			case Window::Camera: return "Camera";
			case Window::Controls: return "Controls";
			default: return "";
			}
		}

		// --- Layout ---
		//
		//  +-----------------------------------------------------------------+
		//  | menu bar                                               [Find  ] |
		//  +-----------+---------------------------------+---------+---------+
		//  | Hierarchy |                                 |  tool   |Inspector|
		//  |           |            the view             | windows |         |
		//  +-----------+---------------------------------+---------+---------+
		//  | Content Browser                                                 |
		//  +-----------------------------------------------------------------+
		//  | status bar                                                      |

		void EditorShell::Place(Window window, float uiScale) {
			const int index = static_cast<int>(window);
			const ImGuiCond condition = placeNow[index] ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
			placeNow[index] = false;

			const ImVec2 display = ImGui::GetIO().DisplaySize;
			const float gap = 6.0f * uiScale;
			const float top = std::max(menuBarHeight, ImGui::GetFrameHeight()) + gap;
			const float bottom = display.y - std::max(statusBarHeight, ImGui::GetFrameHeight()) - gap;
			const float browserHeight = std::min(250.0f * uiScale, (bottom - top) * 0.4f);
			const float columnsBottom = bottom - browserHeight - gap;
			const float columnHeight = std::max(columnsBottom - top, 100.0f);
			const float side = std::min(330.0f * uiScale, display.x * 0.3f);
			const float tool = std::min(400.0f * uiScale, display.x * 0.3f);

			ImVec2 position, size;
			switch (window) {
			case Window::Hierarchy:
				position = ImVec2(gap, top);
				size = ImVec2(side, columnHeight);
				break;
			case Window::Inspector:
				position = ImVec2(display.x - side - gap, top);
				size = ImVec2(side, columnHeight);
				break;
			case Window::ContentBrowser:
				position = ImVec2(gap, bottom - browserHeight);
				size = ImVec2(display.x - 2.0f * gap, browserHeight);
				break;
			case Window::Controls:
				size = ImVec2(std::min(560.0f * uiScale, display.x - 2.0f * gap), std::min(620.0f * uiScale, columnHeight));
				position = ImVec2((display.x - size.x) * 0.5f, top);
				break;
			default: // The tool windows, beside the Inspector
				position = ImVec2(std::max(display.x - side - tool - 2.0f * gap, side + 2.0f * gap), top);
				size = ImVec2(tool, columnHeight);
				break;
			}
			ImGui::SetNextWindowPos(position, condition);
			ImGui::SetNextWindowSize(size, condition);
		}

		void EditorShell::ResetLayout() {
			for (bool& now : placeNow) {
				now = true;
			}
		}

		// --- Commands ---

		void EditorShell::BeginFrame() {
			commands.clear();
		}

		void EditorShell::Add(Command command) {
			commands.push_back(std::move(command));
		}

		void EditorShell::Add(const std::string& path, std::function<void()> run, const std::string& shortcut, bool enabled, int checked,
			const std::string& keywords) {
			Command command;
			command.path = path;
			command.run = std::move(run);
			command.shortcut = shortcut;
			command.enabled = enabled;
			command.checked = checked;
			command.keywords = keywords;
			commands.push_back(std::move(command));
		}

		void EditorShell::Separator(const std::string& menu) {
			Command command;
			command.path = menu + "/-";
			commands.push_back(std::move(command));
		}

		void EditorShell::DrawMenuLevel(const std::string& prefix) {
			// The entries directly under prefix, in the order they were added
			std::vector<std::string> done;
			for (const Command& command : commands) {
				if (command.path.compare(0, prefix.size(), prefix) != 0) {
					continue;
				}
				const std::string rest = command.path.substr(prefix.size());
				const size_t slash = rest.find('/');

				if (rest == "-") {
					ImGui::Separator();
					continue;
				}
				if (slash == std::string::npos) {
					bool checked = command.checked == 1;
					if (ImGui::MenuItem(rest.c_str(), command.shortcut.empty() ? nullptr : command.shortcut.c_str(),
						command.checked >= 0 ? &checked : nullptr, command.enabled) && command.run) {
						command.run();
					}
					continue;
				}

				const std::string submenu = rest.substr(0, slash);
				if (std::find(done.begin(), done.end(), submenu) != done.end()) {
					continue;
				}
				done.push_back(submenu);
				if (ImGui::BeginMenu(submenu.c_str())) {
					DrawMenuLevel(prefix + submenu + "/");
					ImGui::EndMenu();
				}
			}
		}

		std::vector<const EditorShell::Command*> EditorShell::FindMatches() const {
			// Every word typed must appear in the command's path or keywords
			std::vector<std::string> words;
			std::string word;
			for (const char* c = find; ; c++) {
				if (*c == ' ' || *c == '\0') {
					if (!word.empty()) {
						words.push_back(Lower(word));
					}
					word.clear();
					if (*c == '\0') {
						break;
					}
				}
				else {
					word += *c;
				}
			}

			std::vector<const Command*> matches;
			if (words.empty()) {
				return matches;
			}
			for (const Command& command : commands) {
				if (IsSeparator(command.path) || !command.run) {
					continue;
				}
				const std::string text = Lower(command.path + " " + command.keywords + " " + command.shortcut);
				bool all = true;
				for (const std::string& w : words) {
					all = all && text.find(w) != std::string::npos;
				}
				if (all) {
					matches.push_back(&command);
				}
			}
			return matches;
		}

		void EditorShell::DrawMenuBar(float uiScale) {
			if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
				FocusFind();
			}

			// A new window size (fullscreen, resizing): the windows go where they belong for it
			const ImVec2 display = ImGui::GetIO().DisplaySize;
			if (display.x != lastDisplay.x || display.y != lastDisplay.y) {
				if (lastDisplay.x > 0.0f) {
					ResetLayout();
				}
				lastDisplay = display;
			}

			if (!ImGui::BeginMainMenuBar()) {
				return;
			}
			menuBarHeight = ImGui::GetWindowHeight();

			// The top level menus, in the order of their first command
			std::vector<std::string> menus;
			for (const Command& command : commands) {
				const std::string menu = command.path.substr(0, command.path.find('/'));
				if (std::find(menus.begin(), menus.end(), menu) == menus.end()) {
					menus.push_back(menu);
				}
			}
			for (const std::string& menu : menus) {
				if (ImGui::BeginMenu(menu.c_str())) {
					DrawMenuLevel(menu + "/");
					ImGui::EndMenu();
				}
			}

			// Find, at the right end
			const float width = 300.0f * uiScale;
			const float x = ImGui::GetWindowWidth() - width - ImGui::GetStyle().ItemSpacing.x * 2.0f;
			if (x > ImGui::GetCursorPosX()) {
				ImGui::SetCursorPosX(x);
			}
			ImGui::SetNextItemWidth(width);
			const bool enter = ImGui::InputTextWithHint("##find", "Find a command (F1)", find, sizeof(find), ImGuiInputTextFlags_EnterReturnsTrue);
			if (ImGui::IsItemEdited()) {
				findSelected = 0;
			}
			findActive = ImGui::IsItemActive();
			const ImVec2 below(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y);

			std::vector<const Command*> matches = FindMatches();
			if (!matches.empty()) {
				if (findActive && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
					findSelected = std::min(findSelected + 1, static_cast<int>(matches.size()) - 1);
				}
				if (findActive && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
					findSelected = std::max(findSelected - 1, 0);
				}
				findSelected = std::clamp(findSelected, 0, static_cast<int>(matches.size()) - 1);
				if (enter && matches[findSelected]->enabled) {
					const auto run = matches[findSelected]->run;
					find[0] = '\0';
					ImGui::EndMainMenuBar();
					run();
					return;
				}
			}
			ImGui::EndMainMenuBar();

			if (paletteOpen) {
				resultsHovered = false;
				DrawPalette(uiScale);
			}
			else if (find[0] != '\0' && (findActive || resultsHovered)) {
				DrawFindResults(below, width);
			}
			else {
				resultsHovered = false;
			}
		}

		const EditorShell::Command* EditorShell::DrawMatches(const std::vector<const Command*>& matches, float width, bool followSelection) {
			if (matches.empty()) {
				ImGui::TextDisabled("No command has those words");
			}
			const Command* chosen = nullptr;
			for (int i = 0; i < static_cast<int>(matches.size()); i++) {
				const Command& command = *matches[i];
				ImGui::PushID(i);
				std::string label = Readable(command.path);
				if (command.checked >= 0) {
					label += command.checked ? "  (on)" : "  (off)";
				}
				ImGui::BeginDisabled(!command.enabled);
				if (ImGui::Selectable(label.c_str(), i == findSelected)) {
					chosen = &command;
				}
				ImGui::EndDisabled();
				if (i == findSelected && followSelection) {
					ImGui::SetScrollHereY();
				}
				if (!command.shortcut.empty()) {
					ImGui::SameLine(width - ImGui::CalcTextSize(command.shortcut.c_str()).x - ImGui::GetStyle().WindowPadding.x * 2.0f);
					ImGui::TextDisabled("%s", command.shortcut.c_str());
				}
				ImGui::PopID();
			}
			return chosen;
		}

		void EditorShell::DrawPalette(float uiScale) {
			const ImVec2 display = ImGui::GetIO().DisplaySize;
			const float width = std::min(640.0f * uiScale, display.x - 40.0f);
			ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, menuBarHeight + 60.0f * uiScale), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
			const bool firstFrame = paletteFocus;
			if (paletteFocus) {
				ImGui::SetNextWindowFocus();
			}

			bool keep = true;
			const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize
				| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize;
			const Command* chosen = nullptr;
			if (ImGui::Begin("Find a Command", &keep, flags)) {
				if (paletteFocus) {
					ImGui::SetKeyboardFocusHere();
					paletteFocus = false;
				}
				ImGui::SetNextItemWidth(-1.0f);
				const bool enter = ImGui::InputTextWithHint("##palette", "Type part of a command: light, save, trees, denoiser...", find, sizeof(find),
					ImGuiInputTextFlags_EnterReturnsTrue);
				if (ImGui::IsItemEdited()) {
					findSelected = 0;
				}
				const bool typing = ImGui::IsItemActive();

				const std::vector<const Command*> matches = FindMatches();
				if (!matches.empty()) {
					if (typing && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
						findSelected = std::min(findSelected + 1, static_cast<int>(matches.size()) - 1);
					}
					if (typing && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
						findSelected = std::max(findSelected - 1, 0);
					}
					findSelected = std::clamp(findSelected, 0, static_cast<int>(matches.size()) - 1);
					if (enter && matches[findSelected]->enabled) {
						chosen = matches[findSelected];
					}
				}

				if (find[0] != '\0') {
					ImGui::BeginChild("##matches", ImVec2(0.0f, std::min(display.y * 0.5f,
						(ImGui::GetTextLineHeightWithSpacing() + 2.0f) * static_cast<float>(std::max<size_t>(matches.size(), 1)) + 8.0f)));
					if (const Command* clicked = DrawMatches(matches, width - ImGui::GetStyle().WindowPadding.x * 2.0f, typing)) {
						chosen = clicked;
					}
					ImGui::EndChild();
				}
				ImGui::TextDisabled("Up / Down to choose, Enter to run, Esc or a click elsewhere to close");

				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
					keep = false;
				}
				// A click elsewhere closes it
				if (!firstFrame && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
					keep = false;
				}
			}
			ImGui::End();

			if (chosen) {
				keep = false;
			}
			if (!keep) {
				paletteOpen = false;
				find[0] = '\0';
			}
			if (chosen && chosen->run) {
				const auto run = chosen->run;
				run();
			}
		}

		void EditorShell::DrawFindResults(const ImVec2& below, float width) {
			const std::vector<const Command*> matches = FindMatches();
			const ImVec2 display = ImGui::GetIO().DisplaySize;
			const float resultsWidth = std::max(width * 1.6f, 360.0f);
			ImGui::SetNextWindowPos(ImVec2(std::min(below.x, display.x - resultsWidth - 4.0f), below.y + 2.0f));
			ImGui::SetNextWindowSizeConstraints(ImVec2(resultsWidth, 0.0f), ImVec2(resultsWidth, display.y * 0.6f));
			const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
				| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
			if (ImGui::Begin("##findResults", nullptr, flags)) {
				ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
				const Command* chosen = DrawMatches(matches, resultsWidth, findActive);
				ImGui::Separator();
				ImGui::TextDisabled("Up / Down to choose, Enter to run");
				resultsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

				if (chosen) {
					const auto run = chosen->run;
					find[0] = '\0';
					resultsHovered = false;
					ImGui::End();
					run();
					return;
				}
			}
			ImGui::End();
		}

		void EditorShell::DrawStatusBar(const std::string& text, bool isError, const std::string& hint, float uiScale) {
			const ImVec2 display = ImGui::GetIO().DisplaySize;
			const float height = ImGui::GetFrameHeight() + 4.0f * uiScale;
			statusBarHeight = height;
			ImGui::SetNextWindowPos(ImVec2(0.0f, display.y - height));
			ImGui::SetNextWindowSize(ImVec2(display.x, height));
			const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
				| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * uiScale, 2.0f * uiScale));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			if (ImGui::Begin("##statusBar", nullptr, flags)) {
				ImGui::AlignTextToFramePadding();
				if (isError) {
					ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", text.c_str());
				}
				else {
					ImGui::TextUnformatted(text.c_str());
				}
				if (!hint.empty()) {
					const float hintWidth = ImGui::CalcTextSize(hint.c_str()).x;
					ImGui::SameLine(std::max(display.x - hintWidth - 12.0f * uiScale, ImGui::GetCursorPosX() + 20.0f * uiScale));
					ImGui::TextDisabled("%s", hint.c_str());
				}
			}
			ImGui::End();
			ImGui::PopStyleVar(2);
		}
	}
}
