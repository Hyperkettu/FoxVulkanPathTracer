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
//   WaterComponent      it is a body of water, simulated (shallow water equations) and refracting
//   PhysicsComponent    it is a rigid body (Jolt Physics, Scene/Physics.h): it falls, collides and floats in play mode
//
// Adding a component type (e.g. audio):
//   1. the struct here
//   2. its inspector entry: COMPONENT_EDITORS in Editor/SceneGraphEditor.cpp
//   3. how it is saved: COMPONENT_SERIALIZERS in Scene/SceneFile.cpp
//   4. a system that acts on it (e.g. stepped from MainRayTracingScene::Update, as the mesh sync is)
namespace Fox {

	namespace Scene {

		class WaterSimulation;

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

		// The entity is a body of water (Scene/WaterSimulation.h): a rectangle of water on the ground, centred where
		// the entity is, filled up to its height, then left to flow (over the terrain, into hollows) and stirred by
		// wind, rain and splashes. Its rotation and scale are not used.
		struct WaterComponent {
			bool enabled = true;
			glm::vec2 size{ 30.0f, 30.0f }; // Metres along x and z
			float cellSize = 0.2f;          // Metres; at most 512 cells a side

			// Its look: clear water is a dielectric (index of refraction 1.333) that absorbs red first
			glm::vec3 color{ 0.62f, 0.85f, 0.86f }; // What white light is tinted to after `clarity` metres in it
			float clarity = 3.0f;                   // Metres
			float roughness = 0.02f;
			float ripples = 1.0f;     // Strength of the wind's ripples finer than the simulation (shading)

			// Its motion
			float viscosity = 0.001f;  // m^2/s (eddy viscosity: the turbulence the grid cannot resolve; water itself: 1e-6)
			float friction = 0.003f;   // Bed friction
			float windSpeed = 6.0f;    // m/s, 10 m above the water
			float windDirection = 30.0f; // Degrees: where it blows to, from +x towards +z
			float gust = 0.6f;         // 0..1
			float rain = 0.0f;         // Drops per square metre and second
			float dropSize = 0.4f;     // Metres (the ring it makes; at least two cells)
			float dropStrength = 0.02f; // Metres
			bool splashOnClick = true; // Clicking it in edit mode splashes it

			// Foam and spray
			float foam = 1.0f;         // How much foam breaking and churning water makes
			float foamLifetime = 6.0f; // Seconds for foam to fade to a third
			float spray = 1.0f;        // How many droplets are thrown up
			float dropletSize = 0.01f; // Metres (radius)
			uint32_t sprayDroplets = 16384; // Room for droplets in flight (0: no spray)

			// Set by the scene
			bool placed = false;
			size_t surface = 0;
			size_t spraySurface = 0;
			bool hasSpray = false;
			std::shared_ptr<WaterSimulation> simulation;
			glm::mat4 placedMatrix{ 1.0f };

			Fox::Graphics::Vulkan::Material ToMaterial() const {
				Fox::Graphics::Vulkan::Material material;
				material.baseColor[0] = material.baseColor[1] = material.baseColor[2] = 1.0f;
				material.roughness = roughness;
				material.metallic = 0.0f;
				material.transmission = 1.0f;
				material.ior = 1.333f;
				const float tint[3] = { color.r, color.g, color.b };
				material.SetAttenuation(tint, clarity);
				// Water (Shaders/water_waves.slang): the wind (m/s, x and z) and the ripples' strength in fields water
				// has no other use for
				material.layerMaterial = -2;
				const float angle = glm::radians(windDirection);
				material.uvScale[0] = std::cos(angle) * windSpeed;
				material.uvScale[1] = std::sin(angle) * windSpeed;
				material.normalScale = ripples;
				return material;
			}
		};

		// The entity is a rigid body (Scene/Physics.h), simulated while physics plays (Physics > Play, F5): a dynamic
		// body falls and collides and its transform follows, a kinematic one follows its transform and pushes what is
		// in the way, a static one stays. Dimensions are in the entity's space: its world scale scales them.
		struct PhysicsComponent {
			enum class Motion { Static, Dynamic, Kinematic };
			// Auto: a sphere mesh's sphere, a model's convex hull (moving) or its triangles (static), a tree's trunk
			// (a capsule), or else the box
			enum class Shape { Auto, Box, Sphere, Capsule, ConvexHull, Mesh };

			bool enabled = true;
			Motion motion = Motion::Dynamic;
			Shape shape = Shape::Auto;
			glm::vec3 halfExtents{ 0.5f };  // Box
			float radius = 0.5f;            // Sphere, capsule
			float halfHeight = 0.5f;        // Capsule: half its straight part, along y
			glm::vec3 center{ 0.0f };       // The shape's centre, from the entity's origin

			float mass = 0.0f;              // kg; 0: from the density and the shape's volume
			float density = 500.0f;         // kg/m^3 (water 1000: lighter floats)
			float friction = 0.5f;
			float restitution = 0.2f;       // Bounciness
			float linearDamping = 0.05f;
			float angularDamping = 0.05f;
			bool continuous = false;        // Swept collision, for fast small bodies

			// In water (a WaterComponent's): lifted by what it displaces, slowed and carried by the flow, and it splashes
			bool floats = true;
			float waterDrag = 0.5f;

			// Set while physics plays
			uint32_t body = 0xffffffffu;

			static const char* MotionName(Motion motion) {
				switch (motion) {
				case Motion::Static: return "Static";
				case Motion::Dynamic: return "Dynamic";
				default: return "Kinematic";
				}
			}
			static const char* ShapeName(Shape shape) {
				switch (shape) {
				case Shape::Auto: return "Auto";
				case Shape::Box: return "Box";
				case Shape::Sphere: return "Sphere";
				case Shape::Capsule: return "Capsule";
				case Shape::ConvexHull: return "Convex hull";
				default: return "Triangle mesh";
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
