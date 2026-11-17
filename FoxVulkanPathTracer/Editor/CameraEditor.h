#pragma once

namespace Fox {

	namespace Editor {

		// The camera window (edit mode, E key): flight speed, and camera paths: keys taken from the view,
		// the path's speed, easing, loop and where the camera looks, playback (P) and a time slider. The active
		// path is drawn in the view.
		class CameraEditor {
		public:
			// Returns true when it put the camera somewhere else (the renderer drops its image history)
			bool Draw(Fox::Scene::CameraManager& manager, Fox::Graphics::Vulkan::Camera& camera,
				const TransformGizmo::View& view, float uiScale);

			// A key where the camera is, after the selected one (K); makes a path if there is none
			void AddKeyFromView(Fox::Scene::CameraManager& manager, Fox::Graphics::Vulkan::Camera& camera);

		private:
			void DrawPath(const Fox::Scene::CameraPath& path, const TransformGizmo::View& view, float uiScale) const;

			int selectedKey = -1;
			bool showPath = true;
		};
	}
}
