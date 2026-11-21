#pragma once

struct ImGuiStyle;

namespace Fox {

	namespace Editor {

		// The editor's look, matching the splash screen (Platform/Splash.cpp) and the Fox Engine logo: deep navy panels,
		// the logo's blue glow for what is hovered and selected, its orange for marks and values, rounded corners
		void ApplyFoxTheme(ImGuiStyle& style);

		// The editor's font: Segoe UI as on the splash screen (Windows), else ImGui's own. Before the backend is set up.
		void LoadFoxFont();
	}
}
