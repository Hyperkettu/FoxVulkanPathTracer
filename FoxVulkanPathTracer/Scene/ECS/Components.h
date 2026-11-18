#pragma once

#include <string>
#include <vector>
#include <memory>

#include "FoxVulkanPathTracer/Scene/ECS/Entity.h"

// The components of scene entities.
//
// Every entity of a SceneGraph has a NameComponent, a HierarchyComponent and a WorldTransformComponent (kept by
// the graph), and a TransformComponent unless it was removed. The rest describe what the entity is:
//
//   TransformComponent  where it is, relative to its parent (on by default)
//   MeshComponent       it is rendered: a glTF model, a sphere or a generated tree
//
// Adding a component type (e.g. physics or audio):
//   1. the struct here
//   2. its inspector entry: COMPONENT_EDITORS in Editor/SceneGraphEditor.cpp
//   3. how it is saved: COMPONENT_SERIALIZERS in Scene/SceneFile.cpp
//   4. a system that acts on it (e.g. stepped from MainRayTracingScene::Update, as the mesh sync is)
namespace Fox {

	namespace Scene {

		using Transform = Fox::Scene::RayTracing::RayTracingScene::ObjectTransform;

		// What the entity is called, and whether it (and everything under it) is in the scene at all
		struct NameComponent {
			std::string name;
			bool active = true;
		};

		// The scene graph: the entity's parent and its children in order
		struct HierarchyComponent {
			Fox::ECS::Entity parent;
			std::vector<Fox::ECS::Entity> children;
		};

		// Position, rotation and scale relative to the parent (to the world for a root)
		struct TransformComponent {
			Transform local;
		};

		// Where the entity is in the world: the parent's world matrix times its local transform (just the
		// parent's without a TransformComponent). Kept by SceneGraph::UpdateTransforms.
		struct WorldTransformComponent {
			glm::mat4 matrix{ 1.0f };
		};

		// The entity is rendered with this mesh
		struct MeshComponent {
			enum class Type { Model, Sphere, Tree };

			bool enabled = true; // Unchecked: stays in the scene, hidden (no rebuild to toggle)
			Type type = Type::Model;

			// Model: a glTF file
			std::string file;          // .glb / .gltf, from the working directory
			bool displacement = false; // Relief reconstructed from its normal maps (slower to load)

			// Sphere
			float radius = 1.0f;
			SurfaceMaterial material;

			// Tree: generated from its settings (see Editor/TreeEditor)
			std::shared_ptr<Tree> tree;

			// Set by the scene when it is placed in the ray tracing scene
			bool placed = false;
			size_t object = 0;              // Its ray tracing scene object
			glm::mat4 placedMatrix{ 1.0f }; // The world matrix it was last given
			bool shown = true;              // The visibility it was last given

			static const char* TypeName(Type type) {
				switch (type) {
				case Type::Model: return "Model";
				case Type::Sphere: return "Sphere";
				default: return "Tree";
				}
			}
		};
	}
}
