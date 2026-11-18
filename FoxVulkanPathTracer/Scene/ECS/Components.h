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
//   LightComponent      it gives light: a directional light (the sun), a point light or a spot light
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

		// The entity gives light. It shines along its -Z axis (as glTF lights do): a directional light's entity
		// is turned to aim it (its position does not matter), point and spot lights are where the entity is.
		struct LightComponent {
			enum class Type { Directional, Point, Spot };

			bool enabled = true; // Unchecked: dark, without a rebuild
			Type type = Type::Directional;
			glm::vec3 color{ 1.0f };
			// Directional: irradiance on a surface facing it (times colour). Point / spot: intensity, the irradiance
			// it gives 1 m away (falls off with the square of the distance).
			float intensity = 10.0f;
			float angularRadius = 0.27f;   // Directional: apparent radius of its disc in degrees (the real sun 0.27). Larger = softer shadows
			float radius = 0.0f;           // Point / spot: radius of the bulb in metres. 0 = hard shadows
			float innerConeAngle = 20.0f;  // Spot: full light within this angle of its axis (degrees)
			float outerConeAngle = 30.0f;  // Spot: none beyond this one

			// Set by the scene when it is placed in the ray tracing scene
			bool placed = false;
			size_t index = 0;                      // In the scene's light buffer
			Fox::Graphics::Vulkan::Light uploaded; // As it was last given to the scene

			// The shader's light for an entity with this world matrix
			Fox::Graphics::Vulkan::Light ToLight(const glm::mat4& world, bool shown) const {
				Fox::Graphics::Vulkan::Light light{};
				const glm::vec3 back = glm::vec3(world[2]); // +Z: where it shines from
				const glm::vec3 towardsLight = glm::dot(back, back) > 1e-12f ? glm::normalize(back) : glm::vec3(0.0f, 1.0f, 0.0f);
				light.color = shown ? color * intensity : glm::vec3(0.0f);
				switch (type) {
				case Type::Directional:
					light.type = 0;
					light.normal = towardsLight;
					light.area = glm::radians(std::max(angularRadius, 0.001f));
					break;
				case Type::Point:
				case Type::Spot: {
					light.type = type == Type::Point ? 1 : 2;
					light.position = glm::vec3(world[3]);
					light.normal = -towardsLight;
					light.area = std::max(radius, 0.0f);
					const float outer = glm::radians(glm::clamp(outerConeAngle, 0.1f, 179.0f));
					const float inner = glm::radians(glm::clamp(std::min(innerConeAngle, outerConeAngle - 0.05f), 0.0f, 179.0f));
					light.tangentX = glm::vec3(std::cos(outer), std::cos(inner), 0.0f);
					break;
				}
				}
				return light;
			}

			static bool Same(const Fox::Graphics::Vulkan::Light& a, const Fox::Graphics::Vulkan::Light& b) {
				return a.type == b.type && a.position == b.position && a.normal == b.normal && a.tangentX == b.tangentX
					&& a.color == b.color && a.area == b.area;
			}

			static const char* TypeName(Type type) {
				switch (type) {
				case Type::Directional: return "Directional light";
				case Type::Point: return "Point light";
				default: return "Spot light";
				}
			}
		};
	}
}
