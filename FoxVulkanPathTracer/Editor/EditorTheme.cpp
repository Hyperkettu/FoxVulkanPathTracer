#include "FoxRenderer.h"

#include "FoxVulkanPathTracer/Editor/EditorTheme.h"

#include <filesystem>

namespace Fox {

	namespace Editor {

		namespace {
			ImVec4 Rgb(int r, int g, int b, float a = 1.0f) {
				return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
			}
		}

		void ApplyFoxTheme(ImGuiStyle& style) {
			// The splash screen's colours: its background, the logo's blue glow and the orange of its letters
			const ImVec4 navy = Rgb(4, 8, 20);
			const ImVec4 panel = Rgb(9, 15, 31, 0.96f);
			const ImVec4 frame = Rgb(18, 28, 50);
			const ImVec4 frameHover = Rgb(28, 44, 78);
			const ImVec4 frameActive = Rgb(36, 58, 102);
			const ImVec4 blue = Rgb(58, 141, 255);
			const ImVec4 blueDim = Rgb(30, 78, 150);
			const ImVec4 orange = Rgb(255, 146, 30);
			const ImVec4 orangeBright = Rgb(255, 184, 64);
			const ImVec4 text = Rgb(226, 233, 246);
			const ImVec4 textDim = Rgb(118, 134, 162);
			const ImVec4 border = Rgb(42, 66, 112, 0.55f);

			ImVec4* c = style.Colors;
			c[ImGuiCol_Text] = text;
			c[ImGuiCol_TextDisabled] = textDim;
			c[ImGuiCol_WindowBg] = panel;
			c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
			c[ImGuiCol_PopupBg] = Rgb(8, 13, 28, 0.98f);
			c[ImGuiCol_Border] = border;
			c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

			c[ImGuiCol_FrameBg] = frame;
			c[ImGuiCol_FrameBgHovered] = frameHover;
			c[ImGuiCol_FrameBgActive] = frameActive;

			c[ImGuiCol_TitleBg] = navy;
			c[ImGuiCol_TitleBgActive] = Rgb(14, 30, 64);
			c[ImGuiCol_TitleBgCollapsed] = Rgb(4, 8, 20, 0.75f);
			c[ImGuiCol_MenuBarBg] = Rgb(7, 12, 26);

			c[ImGuiCol_ScrollbarBg] = Rgb(4, 8, 20, 0.6f);
			c[ImGuiCol_ScrollbarGrab] = Rgb(34, 52, 90);
			c[ImGuiCol_ScrollbarGrabHovered] = blueDim;
			c[ImGuiCol_ScrollbarGrabActive] = blue;

			c[ImGuiCol_CheckMark] = orange;
			c[ImGuiCol_CheckboxSelectedBg] = frameActive;
			c[ImGuiCol_SliderGrab] = blue;
			c[ImGuiCol_SliderGrabActive] = orange;

			c[ImGuiCol_Button] = Rgb(26, 44, 80);
			c[ImGuiCol_ButtonHovered] = blueDim;
			c[ImGuiCol_ButtonActive] = blue;

			c[ImGuiCol_Header] = Rgb(30, 70, 140, 0.55f);
			c[ImGuiCol_HeaderHovered] = Rgb(46, 104, 200, 0.75f);
			c[ImGuiCol_HeaderActive] = blue;

			c[ImGuiCol_Separator] = border;
			c[ImGuiCol_SeparatorHovered] = blueDim;
			c[ImGuiCol_SeparatorActive] = blue;

			c[ImGuiCol_ResizeGrip] = Rgb(58, 141, 255, 0.2f);
			c[ImGuiCol_ResizeGripHovered] = Rgb(58, 141, 255, 0.6f);
			c[ImGuiCol_ResizeGripActive] = orange;

			c[ImGuiCol_InputTextCursor] = orangeBright;

			c[ImGuiCol_Tab] = Rgb(14, 24, 46);
			c[ImGuiCol_TabHovered] = blueDim;
			c[ImGuiCol_TabSelected] = Rgb(26, 50, 96);
			c[ImGuiCol_TabSelectedOverline] = orange;
			c[ImGuiCol_TabDimmed] = Rgb(9, 15, 31);
			c[ImGuiCol_TabDimmedSelected] = Rgb(18, 32, 62);
			c[ImGuiCol_TabDimmedSelectedOverline] = Rgb(255, 146, 30, 0.5f);

			c[ImGuiCol_PlotLines] = blue;
			c[ImGuiCol_PlotLinesHovered] = orangeBright;
			c[ImGuiCol_PlotHistogram] = orange;
			c[ImGuiCol_PlotHistogramHovered] = orangeBright;

			c[ImGuiCol_TableHeaderBg] = Rgb(16, 28, 54);
			c[ImGuiCol_TableBorderStrong] = border;
			c[ImGuiCol_TableBorderLight] = Rgb(42, 66, 112, 0.3f);
			c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
			c[ImGuiCol_TableRowBgAlt] = Rgb(58, 141, 255, 0.04f);

			c[ImGuiCol_TextLink] = blue;
			c[ImGuiCol_TextSelectedBg] = Rgb(58, 141, 255, 0.35f);
			c[ImGuiCol_TreeLines] = border;
			c[ImGuiCol_DragDropTarget] = orange;
			c[ImGuiCol_DragDropTargetBg] = Rgb(255, 146, 30, 0.12f);
			c[ImGuiCol_UnsavedMarker] = orange;
			c[ImGuiCol_NavCursor] = blue;
			c[ImGuiCol_NavWindowingHighlight] = Rgb(255, 255, 255, 0.7f);
			c[ImGuiCol_NavWindowingDimBg] = Rgb(4, 8, 20, 0.5f);
			c[ImGuiCol_ModalWindowDimBg] = Rgb(4, 8, 20, 0.6f);

			// Rounded, like the splash's corners; a little more room than ImGui's defaults for the larger font
			style.WindowRounding = 6.0f;
			style.ChildRounding = 4.0f;
			style.FrameRounding = 4.0f;
			style.PopupRounding = 6.0f;
			style.ScrollbarRounding = 6.0f;
			style.GrabRounding = 4.0f;
			style.TabRounding = 4.0f;
			style.WindowBorderSize = 1.0f;
			style.FrameBorderSize = 0.0f;
			style.PopupBorderSize = 1.0f;
			style.WindowPadding = ImVec2(10.0f, 9.0f);
			style.FramePadding = ImVec2(7.0f, 4.0f);
			style.ItemSpacing = ImVec2(8.0f, 5.0f);
			style.ScrollbarSize = 13.0f;
			style.GrabMinSize = 10.0f;
			style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
			style.SeparatorTextBorderSize = 2.0f;
			style.FontSizeBase = 15.0f;
		}

		void LoadFoxFont() {
			ImGuiIO& io = ImGui::GetIO();
#ifdef _WIN32
			const char* windowsDirectory = SDL_getenv("WINDIR");
			const std::filesystem::path segoe = std::filesystem::path(windowsDirectory ? windowsDirectory : "C:\\Windows") / "Fonts" / "segoeui.ttf";
			std::error_code error;
			if (std::filesystem::exists(segoe, error)) {
				ImFontConfig config;
				config.OversampleH = 2;
				if (io.Fonts->AddFontFromFileTTF(segoe.string().c_str(), 15.0f, &config)) {
					return;
				}
			}
#endif
			io.Fonts->AddFontDefault();
		}
	}
}
