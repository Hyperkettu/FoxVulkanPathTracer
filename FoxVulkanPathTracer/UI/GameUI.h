#pragma once

#include <functional>

namespace Fox {

	namespace UI {

		// The game's user interface: the screen-space texts (TextComponent, space Screen, style Flat) as vector text and
		// the sprites (SpriteComponent), drawn over the image (under the editor's windows) in order of their layer.
		// Positions are fractions of the screen (0, 0 top left); sizes are pixels at 1080 lines (they scale with the
		// window). Text is filled triangles of its glyph outlines with a one pixel anti-aliased edge; a fill image and
		// a mask (alpha, or brightness) can texture and cut it out, as they can sprites.
		//
		// texture: a bindless texture index as ImGui draws it
		void DrawGameUI(Fox::Scene::RayTracing::MainRayTracingScene& scene, ImDrawList* list, ImVec2 display,
			const std::function<ImTextureID(int)>& texture);
	}
}
