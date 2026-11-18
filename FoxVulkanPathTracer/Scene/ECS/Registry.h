#pragma once

#include <vector>
#include <memory>
#include <cassert>
#include <utility>

#include "FoxVulkanPathTracer/Scene/ECS/Entity.h"

namespace Fox {

	namespace ECS {

		// Entities and their components. Any copyable type can be a component; an entity has at most one of each.
		//
		// Components of one type are kept together in a pool (a sparse set: packed arrays of the components and
		// their entities, plus an entity slot -> position table), so going through all components of a type is a
		// walk over a packed array.
		//
		// References to components stay valid until a component of the same type is added or removed.
		class Registry {
		public:
			Registry() = default;
			Registry(Registry&&) noexcept = default;
			Registry& operator=(Registry&&) noexcept = default;
			Registry(const Registry&) = delete;
			Registry& operator=(const Registry&) = delete;

			Entity Create() {
				Entity entity;
				if (!freeSlots.empty()) {
					entity.index = freeSlots.back();
					freeSlots.pop_back();
				}
				else {
					entity.index = static_cast<uint32_t>(generations.size());
					// New slots start at a generation no other registry has given out, so a handle kept from a
					// replaced registry (a scene loaded since) is not mistaken for an entity of this one
					generations.push_back(NextFirstGeneration());
					alive.push_back(false);
				}
				entity.generation = generations[entity.index];
				alive[entity.index] = true;
				aliveCount++;
				return entity;
			}

			// Removes the entity and all its components; its handles stop being valid
			void Destroy(Entity entity) {
				if (!IsValid(entity)) {
					return;
				}
				for (auto& pool : pools) {
					if (pool) {
						pool->Remove(entity.index);
					}
				}
				alive[entity.index] = false;
				generations[entity.index]++;
				freeSlots.push_back(entity.index);
				aliveCount--;
			}

			bool IsValid(Entity entity) const {
				return entity.index < generations.size() && alive[entity.index] && generations[entity.index] == entity.generation;
			}

			size_t GetEntityCount() const { return aliveCount; }

			// Every entity and component. Generations keep counting, so handles from before stay invalid.
			void Clear() {
				for (uint32_t i = 0; i < alive.size(); i++) {
					if (alive[i]) {
						Destroy(Entity{ i, generations[i] });
					}
				}
			}

			// Adds the component (or replaces the one the entity has)
			template<class T, class... Args>
			T& Add(Entity entity, Args&&... args) {
				assert(IsValid(entity));
				return PoolOf<T>().Add(entity, T{ std::forward<Args>(args)... });
			}

			template<class T>
			void Remove(Entity entity) {
				if (IsValid(entity)) {
					if (Pool<T>* pool = FindPool<T>()) {
						pool->Remove(entity.index);
					}
				}
			}

			template<class T>
			bool Has(Entity entity) const {
				return TryGet<T>(entity) != nullptr;
			}

			template<class T>
			T* TryGet(Entity entity) {
				if (!IsValid(entity)) {
					return nullptr;
				}
				Pool<T>* pool = FindPool<T>();
				return pool ? pool->Find(entity.index) : nullptr;
			}

			template<class T>
			const T* TryGet(Entity entity) const {
				return const_cast<Registry*>(this)->TryGet<T>(entity);
			}

			template<class T>
			T& Get(Entity entity) {
				T* component = TryGet<T>(entity);
				assert(component && "Entity has no such component");
				return *component;
			}

			template<class T>
			const T& Get(Entity entity) const {
				return const_cast<Registry*>(this)->Get<T>(entity);
			}

			// f(Entity, T&) for every component of the type. Do not add or remove components of this type in f.
			template<class T, class F>
			void Each(F&& f) {
				if (Pool<T>* pool = FindPool<T>()) {
					for (size_t i = 0; i < pool->components.size(); i++) {
						f(pool->entities[i], pool->components[i]);
					}
				}
			}

			template<class T>
			size_t Count() const {
				const Pool<T>* pool = const_cast<Registry*>(this)->FindPool<T>();
				return pool ? pool->components.size() : 0;
			}

			// Copies of all of from's components onto to (replacing those it has)
			void CopyComponents(Entity from, Entity to) {
				if (!IsValid(from) || !IsValid(to)) {
					return;
				}
				for (auto& pool : pools) {
					if (pool) {
						pool->Copy(from.index, to);
					}
				}
			}

		private:
			struct PoolBase {
				virtual ~PoolBase() = default;
				virtual void Remove(uint32_t index) = 0;
				virtual void Copy(uint32_t from, Entity to) = 0;
			};

			template<class T>
			struct Pool : PoolBase {
				static constexpr uint32_t NONE = UINT32_MAX;
				std::vector<uint32_t> sparse; // Entity slot -> position in the packed arrays
				std::vector<Entity> entities;
				std::vector<T> components;

				T* Find(uint32_t index) {
					return index < sparse.size() && sparse[index] != NONE ? &components[sparse[index]] : nullptr;
				}

				T& Add(Entity entity, T component) {
					if (T* existing = Find(entity.index)) {
						*existing = std::move(component);
						return *existing;
					}
					if (sparse.size() <= entity.index) {
						sparse.resize(entity.index + 1, NONE);
					}
					sparse[entity.index] = static_cast<uint32_t>(components.size());
					entities.push_back(entity);
					components.push_back(std::move(component));
					return components.back();
				}

				void Remove(uint32_t index) override {
					if (index >= sparse.size() || sparse[index] == NONE) {
						return;
					}
					// The last component fills the hole
					const uint32_t position = sparse[index];
					const uint32_t last = static_cast<uint32_t>(components.size() - 1);
					if (position != last) {
						components[position] = std::move(components[last]);
						entities[position] = entities[last];
						sparse[entities[position].index] = position;
					}
					components.pop_back();
					entities.pop_back();
					sparse[index] = NONE;
				}

				void Copy(uint32_t from, Entity to) override {
					if (T* source = Find(from)) {
						T copy = *source; // Before Add, which may move the array
						Add(to, std::move(copy));
					}
				}
			};

			static uint32_t NextFirstGeneration() {
				static uint32_t next = 0;
				return next += 1u << 16;
			}

			static size_t NextTypeId() {
				static size_t next = 0;
				return next++;
			}

			template<class T>
			static size_t TypeId() {
				static const size_t id = NextTypeId();
				return id;
			}

			template<class T>
			Pool<T>* FindPool() {
				const size_t id = TypeId<T>();
				return id < pools.size() ? static_cast<Pool<T>*>(pools[id].get()) : nullptr;
			}

			template<class T>
			Pool<T>& PoolOf() {
				const size_t id = TypeId<T>();
				if (pools.size() <= id) {
					pools.resize(id + 1);
				}
				if (!pools[id]) {
					pools[id] = std::make_unique<Pool<T>>();
				}
				return *static_cast<Pool<T>*>(pools[id].get());
			}

			std::vector<std::unique_ptr<PoolBase>> pools; // By component type id
			std::vector<uint32_t> generations;            // By entity slot
			std::vector<bool> alive;
			std::vector<uint32_t> freeSlots;
			size_t aliveCount = 0;
		};
	}
}
