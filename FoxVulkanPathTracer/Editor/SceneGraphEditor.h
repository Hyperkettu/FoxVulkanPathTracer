#pragma once

#include <string>
#include <vector>

namespace Fox {

	namespace Editor {

		// The scene graph windows (edit mode, E key), after Unity's:
		//
		//   Hierarchy  every entity as a tree. Click to select (the same selection as clicking in the view, which
		//              the gizmos move); drag onto an entity to make it a child, onto the top or bottom edge of
		//              one to place it before or after, onto the empty space below to make it a root. Right-click
		//              for create, duplicate (Ctrl+D), unparent and delete.
		//   Inspector  the selected entity: active, name, and its components, each in a section; Add Component
		//              for more, the section's close button (or right-click on Transform) to remove one.
		//
		// Component types get an inspector entry in COMPONENT_EDITORS (SceneGraphEditor.cpp).
		class SceneGraphEditor {
		public:
			// spawnPosition: where new root entities go (in front of the camera). open: the window's close button.
			void DrawHierarchy(Fox::Scene::RayTracing::MainRayTracingScene& scene, TransformGizmo& gizmo, const glm::vec3& spawnPosition,
				bool* open, float uiScale);
			void DrawInspector(Fox::Scene::RayTracing::MainRayTracingScene& scene, TransformGizmo& gizmo, bool* open, float uiScale);

			// What the component editors share across frames
			struct InspectorState {
				// Rotation shown as Euler angles: kept while the rotation is the one they made, so dragging one
				// angle does not make the others jump
				Fox::ECS::Entity eulerEntity;
				glm::quat eulerRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
				glm::vec3 euler{ 0.0f };

				std::vector<std::string> modelFiles;
				bool modelsListed = false;
			};

		private:
			using Scene = Fox::Scene::RayTracing::MainRayTracingScene;
			using Entity = Fox::ECS::Entity;

			void DrawNode(Scene& scene, TransformGizmo& gizmo, Entity entity);
			void ForgetLostSelection(Scene& scene, TransformGizmo& gizmo);
			void ApplyPending(Scene& scene, TransformGizmo& gizmo, const glm::vec3& spawnPosition);

			// Changes asked for while the tree is drawn, made after it
			struct Move {
				Entity entity;
				Entity parent;
				int index = -1;
			};
			std::vector<Move> moves;
			struct ModelDrop {
				std::string file;
				bool displacement = false;
				Entity parent; // Null: a root
			};
			std::vector<ModelDrop> modelDrops;
			Entity toDelete;
			Entity toDuplicate;
			bool create = false;
			Entity createUnder; // Null: a root

			char nameBuffer[128] = {};
			Entity nameEntity;
			bool nameActive = false;

			InspectorState state;
		};
	}
}
