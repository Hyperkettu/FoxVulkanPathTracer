#pragma once

#include <string>
#include <vector>
#include <functional>

#include "FoxVulkanPathTracer/Scene/ECS/Registry.h"
#include "FoxVulkanPathTracer/Scene/ECS/Components.h"

namespace Fox {

	namespace Scene {

		// The entities of a scene arranged in a tree. Each entity is placed relative to its parent; roots relative
		// to the world. The components live in the registry (GetRegistry); the graph keeps the ones it is built
		// from (NameComponent, HierarchyComponent, WorldTransformComponent) consistent.
		class SceneGraph {
		public:
			using Entity = Fox::ECS::Entity;

			SceneGraph() = default;
			SceneGraph(SceneGraph&&) noexcept = default;
			SceneGraph& operator=(SceneGraph&&) noexcept = default;

			Fox::ECS::Registry& GetRegistry() { return registry; }
			const Fox::ECS::Registry& GetRegistry() const { return registry; }

			// A new entity with a name, its place in the hierarchy and a TransformComponent (identity: it starts
			// where its parent is). Appended to the parent's children, or the roots.
			Entity CreateEntity(const std::string& name, Entity parent = {});

			// Destroys the entity and everything under it
			void DestroyEntity(Entity entity);

			bool IsValid(Entity entity) const { return registry.IsValid(entity); }
			void Clear();
			size_t GetEntityCount() const { return registry.GetEntityCount(); }

			// A copy of the entity and everything under it, next to it under the same parent. Components are copied
			// as they are; adjust what must not be shared afterwards (see MainRayTracingScene::DuplicateEntity).
			Entity Duplicate(Entity entity);

			// --- Hierarchy ---

			Entity GetParent(Entity entity) const;
			const std::vector<Entity>& GetChildren(Entity entity) const;
			const std::vector<Entity>& GetRoots() const { return roots; }

			// Moves the entity under a new parent (null: a root) at position index among its children (-1: last).
			// keepWorld: it stays where it is in the world (its local transform changes). False when the parent is
			// the entity itself or under it.
			bool SetParent(Entity entity, Entity parent, bool keepWorld = true, int index = -1);

			// Its position among its siblings (or the roots)
			int GetSiblingIndex(Entity entity) const;

			// True when ancestor is entity or above it
			bool IsAncestorOf(Entity ancestor, Entity entity) const;

			// Every entity, parents before children, siblings in order
			std::vector<Entity> DepthFirst() const;
			// The entity and everything under it, parents first
			std::vector<Entity> Subtree(Entity entity) const;

			// --- Name and active ---

			const std::string& GetName(Entity entity) const;
			void SetName(Entity entity, const std::string& name);
			bool IsActive(Entity entity) const;
			void SetActive(Entity entity, bool active);
			// Active, and so is every entity above it
			bool IsActiveInHierarchy(Entity entity) const;

			// --- Transforms ---

			bool HasTransform(Entity entity) const { return registry.Has<TransformComponent>(entity); }
			Transform GetLocalTransform(Entity entity) const;
			void SetLocalTransform(Entity entity, const Transform& transform);

			// Computed from the hierarchy as it is now
			glm::mat4 GetWorldMatrix(Entity entity) const;
			Transform GetWorldTransform(Entity entity) const;
			// Sets the local transform that puts the entity here in the world (no effect without a TransformComponent)
			void SetWorldTransform(Entity entity, const Transform& transform);

			// The world matrices of all entities (WorldTransformComponent), from their local transforms
			void UpdateTransforms();

		private:
			std::vector<Entity>& SiblingsOf(Entity entity);
			void Detach(Entity entity);
			void UpdateSubtree(Entity entity, const glm::mat4& parentWorld);

			Fox::ECS::Registry registry;
			std::vector<Entity> roots;
		};
	}
}
