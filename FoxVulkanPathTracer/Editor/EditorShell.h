#pragma once

#include <string>
#include <vector>
#include <functional>

namespace Fox {

	namespace Editor {

		// The frame around the editor windows (edit mode, E key):
		//
		//   - the menu bar, built every frame from a list of commands ("File/Save Scene", "Window/Trees", ...)
		//   - Find (F1) in the menu bar: type part of any command's name, Enter or click runs it
		//   - which windows are open, and where they go: a tidy layout around the view (Window > Reset Layout)
		//   - the status bar at the bottom
		//
		// Only the Hierarchy is open at first; the Window menu opens the rest.
		class EditorShell {
		public:
			enum class Window { Hierarchy, Inspector, ContentBrowser, Environment, Trees, Terrain, MeshPaint, Camera, Controls, Input, Profiler, Count };

			static const char* WindowTitle(Window window);
			bool IsOpen(Window window) const { return open[static_cast<int>(window)]; }
			bool* OpenFlag(Window window) { return &open[static_cast<int>(window)]; }
			void SetOpen(Window window, bool isOpen) { open[static_cast<int>(window)] = isOpen; }
			void Toggle(Window window) { SetOpen(window, !IsOpen(window)); }
			// Puts the next ImGui window where this one belongs (the first time, or after Reset Layout)
			void Place(Window window, float uiScale);
			void ResetLayout();

			// --- Commands ---

			struct Command {
				std::string path;      // Menu path: "Create/Light/Point Light". "Menu/-" is a separator.
				std::string shortcut;  // Shown beside it
				std::function<void()> run;
				bool enabled = true;
				int checked = -1;      // -1: not a toggle, 0 / 1: shown with a check mark
				std::string keywords;  // More words Find matches
			};

			// Commands are given every frame, in menu order: the menus show the state of that frame
			void BeginFrame();
			void Add(Command command);
			void Add(const std::string& path, std::function<void()> run, const std::string& shortcut = {}, bool enabled = true, int checked = -1,
				const std::string& keywords = {});
			void Separator(const std::string& menu);

			// The menu bar with Find, and the Find palette (F1: a window in the middle that takes the typing)
			void DrawMenuBar(float uiScale);
			void FocusFind() {
				paletteOpen = true;
				paletteFocus = true;
				find[0] = '\0';
				findSelected = 0;
			}

			// The status bar: text on the left (red when an error), hint on the right
			void DrawStatusBar(const std::string& text, bool isError, const std::string& hint, float uiScale);

			float GetMenuBarHeight() const { return menuBarHeight; }

		private:
			void DrawMenuLevel(const std::string& prefix);
			void DrawFindResults(const ImVec2& below, float width);
			void DrawPalette(float uiScale);
			// The matches as a list; the one clicked, if any
			const Command* DrawMatches(const std::vector<const Command*>& matches, float width, bool followSelection);
			std::vector<const Command*> FindMatches() const;

			bool open[static_cast<int>(Window::Count)] = { true }; // The Hierarchy
			bool placeNow[static_cast<int>(Window::Count)] = {};

			std::vector<Command> commands;
			float menuBarHeight = 0.0f;
			float statusBarHeight = 0.0f;

			char find[128] = {};
			bool paletteOpen = false;
			bool paletteFocus = false;
			ImVec2 lastDisplay{ 0.0f, 0.0f }; // A new window size tidies the layout
			bool findActive = false;     // The field has the keyboard
			bool resultsHovered = false; // The results are under the mouse (keep them while it clicks)
			int findSelected = 0;
		};
	}
}
