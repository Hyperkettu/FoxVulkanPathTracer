#pragma once

#include <string>
#include <vector>
#include <functional>

namespace Fox {

	namespace Editor {

		// The Content Browser (Window menu, or Ctrl+Space): the files the scene is made from, under Models/,
		// Scenes/, Textures/ and Fonts/ of the working directory.
		//
		//   Models (.glb / .gltf)  double-click, or right-click > Add to Scene: a new entity in front of the camera.
		//                          Drag one onto an entity in the Hierarchy to add it as that entity's child.
		//   Scenes (.json)         double-click, or right-click > Open: replaces the scene
		//   Images                 listed with their size on disk (for the tree and terrain materials)
		//   Fonts (.ttf / .otf)    double-click: a UI text in that font
		class ContentBrowser {
		public:
			// What the browser asks the editor to do
			struct Actions {
				std::function<void(const std::string& file, bool displacement)> addModel;
				std::function<void(const std::string& file)> openScene;
				std::function<void(const std::string& file)> addFont;
			};

			// Payload of a model dragged out of the browser
			static constexpr const char* MODEL_PAYLOAD = "FOX_MODEL_FILE";
			struct ModelPayload {
				char file[260];
				bool displacement;
			};

			void Draw(bool* open, const Actions& actions, float uiScale);

			// Shows this folder (e.g. "Scenes") the next time it is drawn
			void ShowFolder(const std::string& folder);

		private:
			enum class Kind { Folder, Model, Scene, Image, Font, Other };
			struct Entry {
				std::string path; // From the working directory, forward slashes
				std::string name;
				Kind kind = Kind::Other;
				uintmax_t size = 0;
			};

			void Scan();
			void DrawFolderTree(const std::string& folder, int depth);
			void Activate(const Entry& entry, const Actions& actions);
			static Kind KindOf(const std::string& path, bool directory);
			static const char* KindTag(Kind kind);

			std::string folder = "Models";
			std::vector<Entry> entries;       // In the folder shown
			std::vector<std::string> roots{ "Models", "Scenes", "Textures", "Fonts" };
			bool scanned = false;
			char filter[128] = {};
			bool displacement = false;
			std::string selected;
		};
	}
}
