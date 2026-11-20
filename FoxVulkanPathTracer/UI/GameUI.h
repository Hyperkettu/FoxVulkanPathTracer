#pragma once

#include <functional>

namespace Fox {

	namespace UI {

		// The game's user interface: the screen-space texts (TextComponent, space Screen, style Flat) as vector text and
		// the sprites (SpriteComponent), drawn over the image (under the editor's windows).
		//   Layout (UILayout): each element anchored in its parent's rectangle (the screen, or the nearest UI element
		//     above it in the hierarchy, whose transform, alpha and tint it follows): a point, or stretched between two.
		//     Pixels are of the reference resolution (Environment > UI canvas), so the whole UI scales with the window.
		//     Parents are drawn before their children; siblings in order of their layer.
		//   Animation (UIAnimationComponent): position, scale, rotation, alpha and tint, in steps with easings.
		// Text is filled triangles of its glyph outlines with a one pixel anti-aliased edge; a fill image and a mask
		// (alpha, or brightness) can texture and cut it out, as they can sprites.
		//
		// texture: a bindless texture index as ImGui draws it
		// deltaTime: real seconds since the last frame (animations; they start over when the game starts or stops playing)
		// mouseOrigin, mouseDirection: the ray through the mouse (world UI canvases are clicked where it meets them)
		void DrawGameUI(Fox::Scene::RayTracing::MainRayTracingScene& scene, ImDrawList* list, ImVec2 display, float deltaTime,
			const std::function<ImTextureID(int)>& texture, const glm::vec3& mouseOrigin, const glm::vec3& mouseDirection);

		// The mouse is over a UI button (last frame): the editor then leaves the click to it (no selecting through it)
		bool PointerOverButton();
		// Some UI button is on the screen (last frame): outside edit mode the pointer is then freed to click it
		bool ButtonsShown();

		// Frees what the UI keeps (the world canvases' draw lists): before ImGui's context is destroyed, whose shared
		// data their destructors still use
		void Shutdown();

		// An easing curve: t in 0..1 to the eased fraction (Back and Elastic overshoot)
		float Ease(Fox::Scene::UIAnimationComponent::Easing easing, float t);
		const char* EasingName(Fox::Scene::UIAnimationComponent::Easing easing);
	}
}
