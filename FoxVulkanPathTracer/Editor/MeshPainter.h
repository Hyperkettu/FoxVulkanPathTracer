#pragma once

#include <string>
#include <vector>
#include <memory>
#include <random>

namespace Fox {

	namespace Editor {

		// The Mesh Paint window (edit mode; Window > Mesh Paint, M toggles the brush): paints models and trees onto
		// the ground (the terrain or the ground plane) with a round brush.
		//
		//   Left mouse     paints: fills the brush up to the density, keeping the minimum spacing
		//   Ctrl + left    erases what was painted under the brush
		//   [ and ]        brush radius
		//
		// Each painted mesh stands on the ground where it lands: models with their lowest point on it (optionally
		// tilted to the slope), trees upright with their base on it. Painted entities go under "Painted" in the
		// hierarchy, one group per mesh.
		//
		// Trees are painted as instances: each tree in the palette has a few variants (the same settings with other
		// seeds), each variant one geometry shared by all its instances. Instances of the same variant keep apart
		// (Same tree apart): a spot closer to one gets another variant, so neighbours never look identical.
		//
		// While a stroke lasts the new meshes show as dots; the scene rebuilds once when it ends.
		class MeshPainter {
		public:
			using Scene = Fox::Scene::RayTracing::MainRayTracingScene;
			using Entity = Fox::ECS::Entity;

			// The window, and the brush in the view
			void Draw(Scene& scene, const TransformGizmo::View& view, bool* open, float uiScale);

			bool IsActive() const { return active; }
			void SetActive(bool isActive) { active = isActive; }
			void Toggle() { active = !active; }
			void ScaleBrush(float factor) { radius = glm::clamp(radius * factor, 0.5f, 200.0f); }

			// Clicks in the view go to the brush while it is on and the pointer is over the ground
			bool MouseDown(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse);
			void MouseUp(Scene& scene);
			// Every frame: paints (or erases) while the button is held
			void Update(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse, bool erase);

			// Puts a mesh's entity onto the ground below it (models by their lowest point, trees by their base)
			static void DropToGround(Scene& scene, Entity entity);

		private:
			// A mesh the brush paints
			struct Entry {
				enum class Kind { Model, Tree };
				Kind kind = Kind::Model;
				std::string name;
				bool enabled = true;
				float weight = 1.0f; // Chance against the other meshes

				// Model
				std::string file;
				bool displacement = false;
				float bottom = 0.0f;       // Lowest point in its own space
				bool alignToGround = false; // Tilted to the slope

				// Tree: variants of the source tree, each one geometry for all its instances
				std::shared_ptr<Fox::Scene::Tree> source;
				int variantCount = 4;
				bool growVariants = true; // More variants when every one has an instance too near
				std::vector<std::shared_ptr<Fox::Scene::Tree>> variants;

				float minScale = 0.85f, maxScale = 1.15f;
				bool randomRotation = true;
				float sink = 0.0f; // Metres into the ground
			};

			// Something painted, for spacing and erasing
			struct Painted {
				Entity entity;
				glm::vec3 position;
				const Fox::Scene::Tree* tree; // Its variant (null for models)
			};

			bool PointUnderMouse(Scene& scene, const TransformGizmo::View& view, glm::vec2 mouse, glm::vec3& point) const;
			void DrawBrush(Scene& scene, const TransformGizmo::View& view, float uiScale) const;
			void DrawPalette(Scene& scene);
			void AddModel(Scene& scene, const std::string& file, bool displacement);
			void AddTree(const std::shared_ptr<Fox::Scene::Tree>& tree, const std::string& name);
			void MakeVariants(Entry& entry);
			Entity GroupOf(Scene& scene, const std::string& name);
			Entity Root(Scene& scene, bool create);
			void GatherPainted(Scene& scene);
			void Paint(Scene& scene);
			void Erase(Scene& scene);
			float Spacing() const;
			bool HasRoomForVariant(Scene& scene, const Entry& entry) const;
			static constexpr size_t MAX_VARIANTS = 16;

			bool active = false;
			float radius = 8.0f;
			float density = 3.0f;       // Meshes per 100 m^2
			float minSpacing = 0.0f;    // Metres between any two; 0: from the density
			float treeSeparation = 15.0f; // Metres between instances of the same tree variant

			std::vector<Entry> entries;
			int modelChoice = 0;
			std::vector<std::string> modelFiles;
			bool modelsListed = false;

			// Stroke
			bool stroking = false;
			bool erasing = false;
			bool hovering = false;
			glm::vec3 hoverPoint{ 0.0f };
			std::vector<Painted> painted;
			std::vector<Entity> added;  // This stroke, not in the scene yet
			std::vector<Entity> erased; // This stroke: hidden now, destroyed when it ends
			bool addedModels = false, addedTrees = false;
			std::mt19937 random{ 12345u };

			std::string status;
		};
	}
}
