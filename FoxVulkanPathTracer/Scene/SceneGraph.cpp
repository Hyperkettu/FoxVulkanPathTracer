#include "FoxRenderer.h"

namespace Fox {

	namespace Scene {

		namespace {
			const std::vector<Fox::ECS::Entity> NO_ENTITIES;
			const std::string NO_NAME;
		}

		SceneGraph::Entity SceneGraph::CreateEntity(const std::string& name, Entity parent) {
			const Entity entity = registry.Create();
			registry.Add<NameComponent>(entity, NameComponent{ name, true });
			registry.Add<HierarchyComponent>(entity);
			registry.Add<TransformComponent>(entity);
			registry.Add<WorldTransformComponent>(entity);

			if (IsValid(parent)) {
				registry.Get<HierarchyComponent>(entity).parent = parent;
				registry.Get<HierarchyComponent>(parent).children.push_back(entity);
			}
			else {
				roots.push_back(entity);
			}
			UpdateSubtree(entity, IsValid(parent) ? registry.Get<WorldTransformComponent>(parent).matrix : glm::mat4(1.0f));
			return entity;
		}

		void SceneGraph::DestroyEntity(Entity entity) {
			if (!IsValid(entity)) {
				return;
			}
			const std::vector<Entity> subtree = Subtree(entity);
			Detach(entity);
			for (Entity e : subtree) {
				registry.Destroy(e);
			}
		}

		void SceneGraph::Clear() {
			registry.Clear();
			roots.clear();
		}

		SceneGraph::Entity SceneGraph::Duplicate(Entity entity) {
			if (!IsValid(entity)) {
				return {};
			}

			// Copies parents first, so every copy has its parent's copy to go under
			std::function<Entity(Entity, Entity)> copy = [&](Entity source, Entity parent) {
				const Entity target = registry.Create();
				registry.CopyComponents(source, target);
				HierarchyComponent& hierarchy = registry.Get<HierarchyComponent>(target);
				hierarchy.parent = parent;
				hierarchy.children.clear();
				if (IsValid(parent)) {
					registry.Get<HierarchyComponent>(parent).children.push_back(target);
				}
				const std::vector<Entity> children = GetChildren(source); // A copy: the arrays may move
				for (Entity child : children) {
					copy(child, target);
				}
				return target;
			};

			const Entity parent = GetParent(entity);
			const Entity duplicate = copy(entity, Entity{});
			// copy() left it parentless and out of the roots: put it just after the original
			registry.Get<HierarchyComponent>(duplicate).parent = parent;
			std::vector<Entity>& siblings = SiblingsOf(duplicate);
			const int index = GetSiblingIndex(entity);
			siblings.insert(siblings.begin() + (index >= 0 ? index + 1 : static_cast<int>(siblings.size())), duplicate);
			UpdateSubtree(duplicate, IsValid(parent) ? GetWorldMatrix(parent) : glm::mat4(1.0f));
			return duplicate;
		}

		// --- Hierarchy ---

		SceneGraph::Entity SceneGraph::GetParent(Entity entity) const {
			const HierarchyComponent* hierarchy = registry.TryGet<HierarchyComponent>(entity);
			return hierarchy ? hierarchy->parent : Entity{};
		}

		const std::vector<SceneGraph::Entity>& SceneGraph::GetChildren(Entity entity) const {
			const HierarchyComponent* hierarchy = registry.TryGet<HierarchyComponent>(entity);
			return hierarchy ? hierarchy->children : NO_ENTITIES;
		}

		std::vector<SceneGraph::Entity>& SceneGraph::SiblingsOf(Entity entity) {
			const Entity parent = GetParent(entity);
			return IsValid(parent) ? registry.Get<HierarchyComponent>(parent).children : roots;
		}

		int SceneGraph::GetSiblingIndex(Entity entity) const {
			const Entity parent = GetParent(entity);
			const std::vector<Entity>& siblings = IsValid(parent) ? GetChildren(parent) : roots;
			const auto it = std::find(siblings.begin(), siblings.end(), entity);
			return it != siblings.end() ? static_cast<int>(it - siblings.begin()) : -1;
		}

		void SceneGraph::Detach(Entity entity) {
			std::vector<Entity>& siblings = SiblingsOf(entity);
			siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());
			registry.Get<HierarchyComponent>(entity).parent = Entity{};
		}

		bool SceneGraph::IsAncestorOf(Entity ancestor, Entity entity) const {
			for (Entity e = entity; IsValid(e); e = GetParent(e)) {
				if (e == ancestor) {
					return true;
				}
			}
			return false;
		}

		bool SceneGraph::SetParent(Entity entity, Entity parent, bool keepWorld, int index) {
			if (!IsValid(entity) || (IsValid(parent) && IsAncestorOf(entity, parent))) {
				return false;
			}

			const glm::mat4 world = GetWorldMatrix(entity);
			const Entity oldParent = GetParent(entity);
			const int oldIndex = GetSiblingIndex(entity);
			Detach(entity);

			// Moving down among the same siblings: the indices after it shifted when it was taken out
			if (oldParent == parent && index > oldIndex && oldIndex >= 0) {
				index--;
			}

			if (IsValid(parent)) {
				registry.Get<HierarchyComponent>(entity).parent = parent;
			}
			std::vector<Entity>& siblings = SiblingsOf(entity);
			index = index < 0 ? static_cast<int>(siblings.size()) : std::min(index, static_cast<int>(siblings.size()));
			siblings.insert(siblings.begin() + index, entity);

			if (keepWorld && HasTransform(entity)) {
				const glm::mat4 parentWorld = IsValid(parent) ? GetWorldMatrix(parent) : glm::mat4(1.0f);
				Transform local = Fox::Scene::RayTracing::RayTracingScene::Decompose(glm::inverse(parentWorld) * world);
				registry.Get<TransformComponent>(entity).local = local;
			}
			UpdateSubtree(entity, IsValid(parent) ? GetWorldMatrix(parent) : glm::mat4(1.0f));
			return true;
		}

		std::vector<SceneGraph::Entity> SceneGraph::Subtree(Entity entity) const {
			std::vector<Entity> result;
			if (!IsValid(entity)) {
				return result;
			}
			std::vector<Entity> stack{ entity };
			while (!stack.empty()) {
				const Entity e = stack.back();
				stack.pop_back();
				result.push_back(e);
				const std::vector<Entity>& children = GetChildren(e);
				for (auto it = children.rbegin(); it != children.rend(); ++it) {
					stack.push_back(*it);
				}
			}
			return result;
		}

		std::vector<SceneGraph::Entity> SceneGraph::DepthFirst() const {
			std::vector<Entity> result;
			result.reserve(registry.GetEntityCount());
			for (Entity root : roots) {
				const std::vector<Entity> subtree = Subtree(root);
				result.insert(result.end(), subtree.begin(), subtree.end());
			}
			return result;
		}

		// --- Name and active ---

		const std::string& SceneGraph::GetName(Entity entity) const {
			const NameComponent* name = registry.TryGet<NameComponent>(entity);
			return name ? name->name : NO_NAME;
		}

		void SceneGraph::SetName(Entity entity, const std::string& name) {
			if (NameComponent* component = registry.TryGet<NameComponent>(entity)) {
				component->name = name;
			}
		}

		bool SceneGraph::IsActive(Entity entity) const {
			const NameComponent* name = registry.TryGet<NameComponent>(entity);
			return name && name->active;
		}

		void SceneGraph::SetActive(Entity entity, bool active) {
			if (NameComponent* component = registry.TryGet<NameComponent>(entity)) {
				component->active = active;
			}
		}

		bool SceneGraph::IsActiveInHierarchy(Entity entity) const {
			if (!IsValid(entity)) {
				return false;
			}
			for (Entity e = entity; IsValid(e); e = GetParent(e)) {
				if (!IsActive(e)) {
					return false;
				}
			}
			return true;
		}

		// --- Transforms ---

		Transform SceneGraph::GetLocalTransform(Entity entity) const {
			const TransformComponent* transform = registry.TryGet<TransformComponent>(entity);
			return transform ? transform->local : Transform{};
		}

		void SceneGraph::SetLocalTransform(Entity entity, const Transform& transform) {
			TransformComponent* component = registry.TryGet<TransformComponent>(entity);
			if (!component) {
				return;
			}
			component->local = transform;
			component->local.rotation = glm::normalize(transform.rotation);
			const Entity parent = GetParent(entity);
			UpdateSubtree(entity, IsValid(parent) ? GetWorldMatrix(parent) : glm::mat4(1.0f));
		}

		glm::mat4 SceneGraph::GetWorldMatrix(Entity entity) const {
			glm::mat4 world(1.0f);
			for (Entity e = entity; IsValid(e); e = GetParent(e)) {
				if (const TransformComponent* transform = registry.TryGet<TransformComponent>(e)) {
					world = transform->local.Matrix() * world;
				}
			}
			return world;
		}

		Transform SceneGraph::GetWorldTransform(Entity entity) const {
			if (!IsValid(GetParent(entity)) && HasTransform(entity)) {
				return GetLocalTransform(entity); // Exactly, not decomposed
			}
			return Fox::Scene::RayTracing::RayTracingScene::Decompose(GetWorldMatrix(entity));
		}

		void SceneGraph::SetWorldTransform(Entity entity, const Transform& transform) {
			if (!HasTransform(entity)) {
				return;
			}
			const Entity parent = GetParent(entity);
			if (!IsValid(parent)) {
				SetLocalTransform(entity, transform);
				return;
			}
			const glm::mat4 parentWorld = GetWorldMatrix(parent);
			Transform local = Fox::Scene::RayTracing::RayTracingScene::Decompose(glm::inverse(parentWorld) * transform.Matrix());
			SetLocalTransform(entity, local);
		}

		void SceneGraph::UpdateSubtree(Entity entity, const glm::mat4& parentWorld) {
			glm::mat4 world = parentWorld;
			if (const TransformComponent* transform = registry.TryGet<TransformComponent>(entity)) {
				world = parentWorld * transform->local.Matrix();
			}
			registry.Get<WorldTransformComponent>(entity).matrix = world;
			const std::vector<Entity> children = GetChildren(entity);
			for (Entity child : children) {
				UpdateSubtree(child, world);
			}
		}

		void SceneGraph::UpdateTransforms() {
			for (Entity root : roots) {
				UpdateSubtree(root, glm::mat4(1.0f));
			}
		}
	}
}
