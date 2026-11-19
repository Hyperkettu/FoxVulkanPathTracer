// Scene files: the content of MainRayTracingScene as JSON (Scenes/*.json).
//
// {
//   "version": 4,
//   "camera":      { "position": [x, y, z], "target": [x, y, z] },
//   "environment": { "ground": true, "groundHeight": -3.73, "groundMaterial": {...} },
//   "entities": [
//     { "name": "Cabin", "active": true, "parent": -1,
//       "components": { "transform": {...}, "mesh": { "type": "model", "file": "Models/cabin.glb", "displacement": true } } },
//     { "name": "Glass ball", "parent": 0,
//       "components": { "transform": {...}, "mesh": { "type": "sphere", "radius": 1.2, "material": {...} } } },
//     { "name": "Tree 1", "components": { "transform": {...}, "mesh": { "type": "tree", "asset": 0 } } },
//     { "name": "Sun", "components": { "transform": {...}, "light": { "type": "directional", "color": [1, 0.58, 0.3], "intensity": 18,
//                                                                        "angularRadius": 1.2 } } }
//   ],
//   "cameraPaths": [ { "name": "Path 1", "speed": 4, "ease": 1.5, "loop": false, "lookAlongPath": false,
//                      "keys": [ { "position": [x, y, z], "target": [x, y, z] }, ... ] } ],
//   "activeCameraPath": 0
// }
//
// Entities are listed parents first; "parent" is the index of the parent in the list (-1 or missing: a root), and
// siblings keep their order. An entity has the components listed (no "transform": it has none).
// A transform is { "position": [x, y, z], "rotation": [x, y, z] (Euler angles, degrees), "scale": [x, y, z] }.
// Version 1 files (lists of "models", "spheres" and "trees" in place of "entities") still load, as root entities.
// Trees are listed once each in "treeAssets": [ { "shape": {...}, "look": {...}, "wind": {...} } ], and a tree mesh refers
// to one by index ({ "type": "tree", "asset": 0 }): entities sharing a tree are its instances. Before version 4 each tree
// mesh held its own shape, look and wind.
// Before version 3 the sun was part of the environment ("sun", "sunDirection", "sunColor", "sunIntensity",
// "sunAngularRadius"): such files get a "Sun" entity with a directional light in its place.
// Every value is optional: what a file leaves out keeps its default, so files stay readable by later versions.

#include "FoxRenderer.h"
#include "FoxVulkanPathTracer/Core/TinyGLTF/json.hpp"

#include <charconv>
#include <filesystem>
#include <unordered_map>

using json = nlohmann::json;

// --- glm ---
namespace nlohmann {
	template<> struct adl_serializer<glm::vec2> {
		static void to_json(json& j, const glm::vec2& v) { j = { v.x, v.y }; }
		static void from_json(const json& j, glm::vec2& v) { v = glm::vec2(j.at(0).get<float>(), j.at(1).get<float>()); }
	};
	template<> struct adl_serializer<glm::vec3> {
		static void to_json(json& j, const glm::vec3& v) { j = { v.x, v.y, v.z }; }
		static void from_json(const json& j, glm::vec3& v) { v = glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()); }
	};
}

namespace {
	// Every number in the scene is a float, but JSON keeps doubles: 0.07f would be written as 0.07000000029802322.
	// Each one is written with the fewest digits that read back as the same float instead.
	void ShortenFloats(json& value) {
		if (value.is_structured()) {
			for (auto& child : value) {
				ShortenFloats(child);
			}
		}
		else if (value.is_number_float()) {
			char text[32];
			const auto result = std::to_chars(text, text + sizeof(text), static_cast<float>(value.get<double>()));
			value = std::strtod(std::string(text, result.ptr).c_str(), nullptr);
		}
	}
}

namespace Fox {

	namespace Graphics {
		namespace Geometry {
			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClosedSpline, points)

			// Everything but the height map callbacks, which come from the tree's look
			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TreeSettings,
				trunkLength, trunkRadius, trunkTaper, trunkCylinders,
				crossSection, crossSectionBase, crossSectionTop, branchCrossSection, trunkCurve,
				branchLevels, firstBranchHeight, trunkBranches, branchBranches, branchAngle, branchLengthRatio, branchRadiusRatio,
				upwardTendency, ringsPerCylinder,
				seed, bendiness, trunkWander, angleVariation, lengthVariation, positionVariation, azimuthVariation,
				leafSizeVariation, leafRollVariation,
				textureSize, displacementScale, displacementMidlevel, detailEdgeLength, minDisplacedRadius,
				leaves, leafLevel, leavesPerCylinder, leafSize, leafColumns, leafRows, leafCurl,
				leafDisplacementScale, leafDisplacementMidlevel)
		}
	}

	namespace Animation {
		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WindSkeleton::Settings,
			windDirection, windSpeed, gustStrength, gustSpeed, turbulence,
			trunkFrequency, maxFrequency, dampingRatio, response, maxAngle)
		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TreeGrowth, enabled, duration, startScale, leafLag, loop)
	}

	namespace Scene {
		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CameraKey, position, target)
		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CameraComponent, enabled, fov, priority)
		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(VolumeComponent, enabled, size, round, softness, density, color, anisotropy,
			glow, glowStrength, noiseSize, noiseStrength, velocity)
		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TerrainLayer, name, albedo, normal, roughness, metallic, occlusion, height,
			normalGreenUp, tiling, tint, roughnessScale, normalStrength)
		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TerrainSettings, size, resolution, blendResolution, position, blendSoftness, layers)
		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CameraPath, name, speed, ease, loop, lookAlongPath, keys)
		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SurfaceMaterial,
			color, roughness, metallic, transmission, ior, attenuationColor, attenuationDistance)
		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SurfaceTextures,
			albedo, normal, roughness, metallic, occlusion, displacement, normalGreenUp, invertDisplacement)
		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TreeLook,
			bark, leaf, barkTint, leafTint, barkRoughness, leafRoughness, alphaCutoff)

		namespace RayTracing {

			using Scene = MainRayTracingScene;

			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene::Fog, enabled, density, falloff, baseHeight, color, anisotropy)
			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene::Clouds, enabled, bottom, top, coverage, density, size, wind, anisotropy, albedo, shadows)
			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene::Environment,
				ground, terrain, groundHeight, groundMaterial, fog, clouds)
			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene::CameraPose, position, target)

			namespace {
				const int FILE_VERSION = 4;

				using Fox::ECS::Entity;
				using Fox::Scene::SceneGraph;
				using Fox::Scene::MeshComponent;
				using Fox::Scene::TransformComponent;
				using Fox::Scene::LightComponent;

				json TransformToJson(const RayTracingScene::ObjectTransform& transform) {
					return {
						{ "position", transform.position },
						{ "rotation", glm::degrees(glm::eulerAngles(transform.rotation)) },
						{ "scale", transform.scale },
					};
				}

				RayTracingScene::ObjectTransform TransformFromJson(const json& j) {
					RayTracingScene::ObjectTransform transform;
					if (!j.is_object()) {
						return transform;
					}
					transform.position = j.value("position", transform.position);
					transform.rotation = glm::quat(glm::radians(j.value("rotation", glm::vec3(0.0f))));
					transform.scale = j.value("scale", transform.scale);
					return transform;
				}

				const char* MeshTypeKey(MeshComponent::Type type) {
					switch (type) {
					case MeshComponent::Type::Model: return "model";
					case MeshComponent::Type::Sphere: return "sphere";
					default: return "tree";
					}
				}

				// Trees are saved once each in "treeAssets", and tree meshes refer to them by index: entities that
				// share a tree (its instances) share it again when loaded
				struct TreeAssets {
					std::unordered_map<const Fox::Scene::Tree*, int> ids; // Saving
					json list = json::array();
					std::vector<std::shared_ptr<Fox::Scene::Tree>> loaded; // Loading
				};
				TreeAssets* treeAssets = nullptr; // While a file is saved or loaded

				json TreeToJson(const Fox::Scene::Tree& tree) {
					return { { "shape", tree.shape }, { "look", tree.look }, { "wind", tree.wind }, { "growth", tree.growth } };
				}

				std::shared_ptr<Fox::Scene::Tree> TreeFromJson(const json& j) {
					auto tree = std::make_shared<Fox::Scene::Tree>();
					tree->shape = j.value("shape", Fox::Graphics::Geometry::TreeSettings{});
					tree->look = j.value("look", Fox::Scene::TreeLook{});
					tree->wind = j.value("wind", Fox::Animation::WindSkeleton::Settings{});
					tree->growth = j.value("growth", Fox::Animation::TreeGrowth{});
					return tree;
				}

				json MeshToJson(const MeshComponent& mesh) {
					json j = { { "type", MeshTypeKey(mesh.type) }, { "enabled", mesh.enabled } };
					switch (mesh.type) {
					case MeshComponent::Type::Model:
						j["file"] = mesh.file;
						j["displacement"] = mesh.displacement;
						break;
					case MeshComponent::Type::Sphere:
						j["radius"] = mesh.radius;
						j["material"] = mesh.material;
						break;
					case MeshComponent::Type::Tree:
						if (mesh.tree && treeAssets) {
							auto [it, added] = treeAssets->ids.emplace(mesh.tree.get(), static_cast<int>(treeAssets->list.size()));
							if (added) {
								treeAssets->list.push_back(TreeToJson(*mesh.tree));
							}
							j["asset"] = it->second;
						}
						else if (mesh.tree) {
							j.update(TreeToJson(*mesh.tree));
						}
						break;
					}
					return j;
				}

				MeshComponent MeshFromJson(const json& j) {
					MeshComponent mesh;
					mesh.enabled = j.value("enabled", true);
					const std::string type = j.value("type", std::string("model"));
					if (type == "sphere") {
						mesh.type = MeshComponent::Type::Sphere;
						mesh.radius = j.value("radius", 1.0f);
						mesh.material = j.value("material", Fox::Scene::SurfaceMaterial{});
					}
					else if (type == "tree") {
						mesh.type = MeshComponent::Type::Tree;
						const int asset = j.value("asset", -1);
						if (treeAssets && asset >= 0 && asset < static_cast<int>(treeAssets->loaded.size())) {
							mesh.tree = treeAssets->loaded[asset]; // Shared: an instance
						}
						else {
							mesh.tree = TreeFromJson(j); // Version 3 and before: its own tree, in place
						}
					}
					else {
						mesh.type = MeshComponent::Type::Model;
						mesh.file = j.value("file", std::string());
						mesh.displacement = j.value("displacement", false);
					}
					return mesh;
				}

				json LightToJson(const LightComponent& light) {
					const char* types[] = { "directional", "point", "spot" };
					json j = {
						{ "type", types[static_cast<int>(light.type)] },
						{ "enabled", light.enabled },
						{ "color", light.color },
						{ "intensity", light.intensity },
					};
					if (light.type == LightComponent::Type::Directional) {
						j["angularRadius"] = light.angularRadius;
					}
					else {
						j["radius"] = light.radius;
					}
					if (light.type == LightComponent::Type::Spot) {
						j["innerConeAngle"] = light.innerConeAngle;
						j["outerConeAngle"] = light.outerConeAngle;
					}
					return j;
				}

				LightComponent LightFromJson(const json& j) {
					LightComponent light;
					const std::string type = j.value("type", std::string("directional"));
					light.type = type == "point" ? LightComponent::Type::Point : type == "spot" ? LightComponent::Type::Spot : LightComponent::Type::Directional;
					light.enabled = j.value("enabled", light.enabled);
					light.color = j.value("color", light.color);
					light.intensity = j.value("intensity", light.intensity);
					light.angularRadius = j.value("angularRadius", light.angularRadius);
					light.radius = j.value("radius", light.radius);
					light.innerConeAngle = j.value("innerConeAngle", light.innerConeAngle);
					light.outerConeAngle = j.value("outerConeAngle", light.outerConeAngle);
					return light;
				}

				json PhysicsToJson(const Fox::Scene::PhysicsComponent& body) {
					return json{
						{ "enabled", body.enabled }, { "motion", static_cast<int>(body.motion) }, { "shape", static_cast<int>(body.shape) },
						{ "halfExtents", body.halfExtents }, { "radius", body.radius }, { "halfHeight", body.halfHeight }, { "center", body.center },
						{ "mass", body.mass }, { "density", body.density }, { "friction", body.friction }, { "restitution", body.restitution },
						{ "linearDamping", body.linearDamping }, { "angularDamping", body.angularDamping }, { "continuous", body.continuous },
						{ "floats", body.floats }, { "waterDrag", body.waterDrag },
					};
				}

				Fox::Scene::PhysicsComponent PhysicsFromJson(const json& j) {
					Fox::Scene::PhysicsComponent body;
					body.enabled = j.value("enabled", body.enabled);
					body.motion = static_cast<Fox::Scene::PhysicsComponent::Motion>(std::clamp(j.value("motion", 1), 0, 2));
					body.shape = static_cast<Fox::Scene::PhysicsComponent::Shape>(std::clamp(j.value("shape", 0), 0, 5));
					body.halfExtents = j.value("halfExtents", body.halfExtents);
					body.radius = j.value("radius", body.radius);
					body.halfHeight = j.value("halfHeight", body.halfHeight);
					body.center = j.value("center", body.center);
					body.mass = j.value("mass", body.mass);
					body.density = j.value("density", body.density);
					body.friction = j.value("friction", body.friction);
					body.restitution = j.value("restitution", body.restitution);
					body.linearDamping = j.value("linearDamping", body.linearDamping);
					body.angularDamping = j.value("angularDamping", body.angularDamping);
					body.continuous = j.value("continuous", body.continuous);
					body.floats = j.value("floats", body.floats);
					body.waterDrag = j.value("waterDrag", body.waterDrag);
					return body;
				}

				json NavAgentToJson(const Fox::Scene::NavAgentComponent& nav) {
					json j{
						{ "enabled", nav.enabled }, { "speed", nav.speed }, { "turnRate", nav.turnRate }, { "radius", nav.radius },
						{ "height", nav.height }, { "maxSlope", nav.maxSlope }, { "maxStep", nav.maxStep },
						{ "maxWaterDepth", nav.maxWaterDepth }, { "cellSize", nav.cellSize }, { "forward", static_cast<int>(nav.forward) },
						{ "alignToGround", nav.alignToGround },
					};
					if (nav.hasTarget) {
						j["target"] = nav.target;
					}
					return j;
				}

				Fox::Scene::NavAgentComponent NavAgentFromJson(const json& j) {
					Fox::Scene::NavAgentComponent nav;
					nav.enabled = j.value("enabled", nav.enabled);
					nav.speed = j.value("speed", nav.speed);
					nav.turnRate = j.value("turnRate", nav.turnRate);
					nav.radius = j.value("radius", nav.radius);
					nav.height = j.value("height", nav.height);
					nav.maxSlope = j.value("maxSlope", nav.maxSlope);
					nav.maxStep = j.value("maxStep", nav.maxStep);
					nav.maxWaterDepth = j.value("maxWaterDepth", nav.maxWaterDepth);
					nav.cellSize = j.value("cellSize", nav.cellSize);
					nav.forward = static_cast<Fox::Scene::NavAgentComponent::Forward>(std::clamp(j.value("forward", 0), 0, 3));
					nav.alignToGround = j.value("alignToGround", nav.alignToGround);
					if (j.contains("target")) {
						nav.target = j.value("target", nav.target);
						nav.hasTarget = true;
					}
					return nav;
				}

				json WaterToJson(const Fox::Scene::WaterComponent& water) {
					return {
						{ "enabled", water.enabled }, { "size", water.size }, { "cellSize", water.cellSize },
						{ "color", water.color }, { "clarity", water.clarity }, { "roughness", water.roughness }, { "ripples", water.ripples },
						{ "viscosity", water.viscosity }, { "friction", water.friction },
						{ "windSpeed", water.windSpeed }, { "windDirection", water.windDirection }, { "gust", water.gust },
						{ "rain", water.rain }, { "dropSize", water.dropSize }, { "dropStrength", water.dropStrength },
						{ "splashOnClick", water.splashOnClick },
						{ "foam", water.foam }, { "foamLifetime", water.foamLifetime }, { "spray", water.spray },
						{ "dropletSize", water.dropletSize }, { "sprayDroplets", water.sprayDroplets },
					};
				}

				Fox::Scene::WaterComponent WaterFromJson(const json& j) {
					Fox::Scene::WaterComponent water;
					water.enabled = j.value("enabled", water.enabled);
					water.size = j.value("size", water.size);
					water.cellSize = j.value("cellSize", water.cellSize);
					water.color = j.value("color", water.color);
					water.clarity = j.value("clarity", water.clarity);
					water.roughness = j.value("roughness", water.roughness);
					water.ripples = j.value("ripples", water.ripples);
					water.viscosity = j.value("viscosity", water.viscosity);
					water.friction = j.value("friction", water.friction);
					water.windSpeed = j.value("windSpeed", water.windSpeed);
					water.windDirection = j.value("windDirection", water.windDirection);
					water.gust = j.value("gust", water.gust);
					water.rain = j.value("rain", water.rain);
					water.dropSize = j.value("dropSize", water.dropSize);
					water.dropStrength = j.value("dropStrength", water.dropStrength);
					water.splashOnClick = j.value("splashOnClick", water.splashOnClick);
					water.foam = j.value("foam", water.foam);
					water.foamLifetime = j.value("foamLifetime", water.foamLifetime);
					water.spray = j.value("spray", water.spray);
					water.dropletSize = j.value("dropletSize", water.dropletSize);
					water.sprayDroplets = j.value("sprayDroplets", water.sprayDroplets);
					return water;
				}

				// How each component type is saved: under "components", by its key. A component type that is saved
				// gets an entry here (see Scene/ECS/Components.h).
				struct ComponentSerializer {
					const char* key;
					// Writes the entity's component into j; false when it has none
					std::function<bool(const SceneGraph&, Entity, json&)> save;
					// Gives the entity the component read from j
					std::function<void(SceneGraph&, Entity, const json&)> load;
				};

				const ComponentSerializer COMPONENT_SERIALIZERS[] = {
					{
						"transform",
						[](const SceneGraph& graph, Entity entity, json& j) {
							if (!graph.HasTransform(entity)) {
								return false;
							}
							j = TransformToJson(graph.GetLocalTransform(entity));
							return true;
						},
						[](SceneGraph& graph, Entity entity, const json& j) {
							graph.GetRegistry().Add<TransformComponent>(entity, TransformComponent{ TransformFromJson(j) });
						},
					},
					{
						"mesh",
						[](const SceneGraph& graph, Entity entity, json& j) {
							const MeshComponent* mesh = graph.GetRegistry().TryGet<MeshComponent>(entity);
							if (!mesh) {
								return false;
							}
							j = MeshToJson(*mesh);
							return true;
						},
						[](SceneGraph& graph, Entity entity, const json& j) {
							graph.GetRegistry().Add<MeshComponent>(entity, MeshFromJson(j));
						},
					},
					{
						"light",
						[](const SceneGraph& graph, Entity entity, json& j) {
							const LightComponent* light = graph.GetRegistry().TryGet<LightComponent>(entity);
							if (!light) {
								return false;
							}
							j = LightToJson(*light);
							return true;
						},
						[](SceneGraph& graph, Entity entity, const json& j) {
							graph.GetRegistry().Add<LightComponent>(entity, LightFromJson(j));
						},
					},
					{
						"water",
						[](const SceneGraph& graph, Entity entity, json& j) {
							const Fox::Scene::WaterComponent* water = graph.GetRegistry().TryGet<Fox::Scene::WaterComponent>(entity);
							if (!water) {
								return false;
							}
							j = WaterToJson(*water);
							return true;
						},
						[](SceneGraph& graph, Entity entity, const json& j) {
							graph.GetRegistry().Add<Fox::Scene::WaterComponent>(entity, WaterFromJson(j));
						},
					},
					{
						"physics",
						[](const SceneGraph& graph, Entity entity, json& j) {
							const Fox::Scene::PhysicsComponent* body = graph.GetRegistry().TryGet<Fox::Scene::PhysicsComponent>(entity);
							if (!body) {
								return false;
							}
							j = PhysicsToJson(*body);
							return true;
						},
						[](SceneGraph& graph, Entity entity, const json& j) {
							graph.GetRegistry().Add<Fox::Scene::PhysicsComponent>(entity, PhysicsFromJson(j));
						},
					},
					{
						"camera",
						[](const SceneGraph& graph, Entity entity, json& j) {
							const Fox::Scene::CameraComponent* camera = graph.GetRegistry().TryGet<Fox::Scene::CameraComponent>(entity);
							if (!camera) {
								return false;
							}
							j = *camera;
							return true;
						},
						[](SceneGraph& graph, Entity entity, const json& j) {
							graph.GetRegistry().Add<Fox::Scene::CameraComponent>(entity, j.get<Fox::Scene::CameraComponent>());
						},
					},
					{
						"volume",
						[](const SceneGraph& graph, Entity entity, json& j) {
							const Fox::Scene::VolumeComponent* volume = graph.GetRegistry().TryGet<Fox::Scene::VolumeComponent>(entity);
							if (!volume) {
								return false;
							}
							j = *volume;
							return true;
						},
						[](SceneGraph& graph, Entity entity, const json& j) {
							graph.GetRegistry().Add<Fox::Scene::VolumeComponent>(entity, j.get<Fox::Scene::VolumeComponent>());
						},
					},
					{
						"navAgent",
						[](const SceneGraph& graph, Entity entity, json& j) {
							const Fox::Scene::NavAgentComponent* nav = graph.GetRegistry().TryGet<Fox::Scene::NavAgentComponent>(entity);
							if (!nav) {
								return false;
							}
							j = NavAgentToJson(*nav);
							return true;
						},
						[](SceneGraph& graph, Entity entity, const json& j) {
							graph.GetRegistry().Add<Fox::Scene::NavAgentComponent>(entity, NavAgentFromJson(j));
						},
					},
				};

				// Before version 3: the environment's sun, as an entity
				void SunFromEnvironment(const json& environment, SceneGraph& graph) {
					if (!environment.is_object()) {
						MainRayTracingScene::CreateSun(graph);
						return;
					}
					if (!environment.value("sun", true)) {
						return;
					}
					MainRayTracingScene::Sun sun;
					sun.direction = environment.value("sunDirection", sun.direction);
					sun.color = environment.value("sunColor", sun.color);
					sun.intensity = environment.value("sunIntensity", sun.intensity);
					sun.angularRadius = environment.value("sunAngularRadius", sun.angularRadius);
					MainRayTracingScene::CreateSun(graph, sun);
				}

				json EntitiesToJson(const SceneGraph& graph) {
					const std::vector<Entity> entities = graph.DepthFirst();
					std::unordered_map<Entity, int> indices;
					for (size_t i = 0; i < entities.size(); i++) {
						indices[entities[i]] = static_cast<int>(i);
					}

					json list = json::array();
					for (Entity entity : entities) {
						json entry = { { "name", graph.GetName(entity) }, { "active", graph.IsActive(entity) } };
						const Entity parent = graph.GetParent(entity);
						entry["parent"] = graph.IsValid(parent) ? indices.at(parent) : -1;
						json components = json::object();
						for (const ComponentSerializer& serializer : COMPONENT_SERIALIZERS) {
							json component;
							if (serializer.save(graph, entity, component)) {
								components[serializer.key] = std::move(component);
							}
						}
						entry["components"] = std::move(components);
						list.push_back(std::move(entry));
					}
					return list;
				}

				void EntitiesFromJson(const json& list, SceneGraph& graph) {
					std::vector<Entity> created;
					for (const json& entry : list) {
						const int parentIndex = entry.value("parent", -1);
						const Entity parent = parentIndex >= 0 && parentIndex < static_cast<int>(created.size()) ? created[parentIndex] : Entity{};
						const Entity entity = graph.CreateEntity(entry.value("name", "Entity " + std::to_string(created.size() + 1)), parent);
						graph.SetActive(entity, entry.value("active", true));

						const json components = entry.value("components", json::object());
						graph.GetRegistry().Remove<TransformComponent>(entity); // Only when listed
						for (const ComponentSerializer& serializer : COMPONENT_SERIALIZERS) {
							if (components.contains(serializer.key)) {
								serializer.load(graph, entity, components.at(serializer.key));
							}
						}
						created.push_back(entity);
					}
				}

				// Version 1: lists of models, spheres and trees, each a root entity
				void Version1FromJson(const json& file, SceneGraph& graph) {
					auto add = [&](const std::string& name, const json& entry, MeshComponent mesh) {
						const Entity entity = graph.CreateEntity(name);
						graph.SetLocalTransform(entity, TransformFromJson(entry.value("transform", json::object())));
						graph.GetRegistry().Add<MeshComponent>(entity, std::move(mesh));
					};

					for (const json& entry : file.value("models", json::array())) {
						MeshComponent mesh;
						mesh.type = MeshComponent::Type::Model;
						mesh.file = entry.at("file").get<std::string>();
						mesh.displacement = entry.value("displacement", false);
						add(entry.value("name", std::filesystem::path(mesh.file).stem().string()), entry, std::move(mesh));
					}
					int spheres = 0;
					for (const json& entry : file.value("spheres", json::array())) {
						MeshComponent mesh;
						mesh.type = MeshComponent::Type::Sphere;
						mesh.radius = entry.value("radius", 1.0f);
						mesh.material = entry.value("material", Fox::Scene::SurfaceMaterial{});
						add(entry.value("name", "Sphere " + std::to_string(++spheres)), entry, std::move(mesh));
					}
					int trees = 0;
					for (const json& entry : file.value("trees", json::array())) {
						json tree = entry;
						tree["type"] = "tree";
						add(entry.value("name", "Tree " + std::to_string(++trees)), entry, MeshFromJson(tree));
					}
				}
			}

			bool MainRayTracingScene::SaveToFile(const std::string& path, const CameraPose& camera, std::string& error) {
				// While the game plays, the scene is saved as it was when play started
				const bool fromSnapshot = physicsPlaying && playScene;

				json file;
				file["version"] = FILE_VERSION;
				file["camera"] = camera;
				file["environment"] = fromSnapshot ? playScene->environment : environment;
				// The terrain's heights and blend map go in files of their own beside the scene file
				if (terrain.IsCreated()) {
					json entry = terrain.settings;
					const std::string base = std::filesystem::path(path).replace_extension("").generic_string();
					const std::string heightsFile = base + ".terrain-heights.bin";
					const std::string blendFile = base + ".terrain-blend.png";
					const std::filesystem::path target(path);
					if (target.has_parent_path()) {
						std::filesystem::create_directories(target.parent_path());
					}
					if (!terrain.SaveHeights(heightsFile, error) || !terrain.SaveBlend(blendFile, error)) {
						return false;
					}
					entry["heights"] = heightsFile;
					entry["blend"] = blendFile;
					file["terrain"] = entry;
				}
				file["cameraPaths"] = cameraManager.GetPaths();
				file["activeCameraPath"] = cameraManager.GetActivePath();
				if (fromSnapshot) {
					file["entities"] = json::parse(playScene->entities);
					file["treeAssets"] = json::parse(playScene->treeAssets);
				}
				else {
					TreeAssets assets;
					treeAssets = &assets;
					file["entities"] = EntitiesToJson(graph);
					treeAssets = nullptr;
					file["treeAssets"] = std::move(assets.list);
				}

				try {
					const std::filesystem::path target(path);
					if (target.has_parent_path()) {
						std::filesystem::create_directories(target.parent_path());
					}
					std::ofstream out(path);
					if (!out) {
						error = "Cannot write " + path;
						return false;
					}
					ShortenFloats(file);
					out << file.dump(2) << "\n";
					return true;
				}
				catch (const std::exception& e) {
					error = path + ": " + e.what();
					return false;
				}
			}

			// --- Play mode: the scene as it was when play started ---

			namespace {
				// Component values to put back on the same entities: fits(now, saved) says whether the component is
				// still the same thing (otherwise the scene is restored the slow way, from JSON); apply(now, value)
				// keeps what the renderer set up in `now` (placements) while value takes the rest from the snapshot
				template <typename T, typename Fits, typename Apply>
				std::function<bool(bool)> SnapshotOf(SceneGraph& graph, Fits fits, Apply apply) {
					std::vector<std::pair<Entity, T>> saved;
					graph.GetRegistry().Each<T>([&](Entity entity, T& component) { saved.emplace_back(entity, component); });
					return [&graph, saved = std::move(saved), fits, apply](bool doApply) {
						size_t now = 0;
						graph.GetRegistry().Each<T>([&](Entity, T&) { now++; });
						if (now != saved.size()) {
							return false;
						}
						for (const auto& [entity, component] : saved) {
							T* current = graph.IsValid(entity) ? graph.GetRegistry().TryGet<T>(entity) : nullptr;
							if (!current || !fits(*current, component)) {
								return false;
							}
							if (doApply) {
								T value = component;
								apply(*current, value);
								*current = std::move(value);
							}
						}
						return true;
					};
				}

				template <typename T>
				std::function<bool(bool)> SnapshotOf(SceneGraph& graph) {
					return SnapshotOf<T>(graph, [](const T&, const T&) { return true; }, [](const T&, T&) {});
				}
			}

			void MainRayTracingScene::TakePlaySnapshot() {
				auto snapshot = std::make_unique<PlaySnapshot>();
				TreeAssets assets;
				treeAssets = &assets;
				const json entities = EntitiesToJson(graph);
				treeAssets = nullptr;
				snapshot->entities = entities.dump();
				snapshot->treeAssets = assets.list.dump();
				snapshot->trees.resize(assets.list.size());
				graph.GetRegistry().Each<MeshComponent>([&](Entity, MeshComponent& mesh) {
					if (mesh.tree) {
						auto found = assets.ids.find(mesh.tree.get());
						if (found != assets.ids.end()) {
							snapshot->trees[found->second] = mesh.tree;
						}
					}
				});
				snapshot->environment = environment;
				snapshot->order = graph.DepthFirst();
				snapshot->rebuilds = rebuildCount;

				// The quick way back: values onto the same entities and components
				std::vector<std::tuple<Entity, std::string, bool>> names;
				for (Entity entity : snapshot->order) {
					names.emplace_back(entity, graph.GetName(entity), graph.IsActive(entity));
				}
				snapshot->restores.push_back([this, names = std::move(names)](bool apply) {
					if (apply) {
						for (const auto& [entity, name, active] : names) {
							graph.SetName(entity, name);
							graph.SetActive(entity, active);
						}
					}
					return true;
				});
				using namespace Fox::Scene;
				snapshot->restores.push_back(SnapshotOf<PhysicsComponent>(graph));
				snapshot->restores.push_back(SnapshotOf<VolumeComponent>(graph));
				snapshot->restores.push_back(SnapshotOf<CameraComponent>(graph));
				snapshot->restores.push_back(SnapshotOf<NavAgentComponent>(graph,
					[](const NavAgentComponent&, const NavAgentComponent&) { return true; },
					[](const NavAgentComponent&, NavAgentComponent& value) { value.Invalidate(); }));
				snapshot->restores.push_back(SnapshotOf<LightComponent>(graph,
					[](const LightComponent&, const LightComponent&) { return true; },
					[](const LightComponent& now, LightComponent& value) {
						value.placed = now.placed;
						value.index = now.index;
						value.uploaded = now.uploaded;
					}));
				snapshot->restores.push_back(SnapshotOf<WaterComponent>(graph,
					[](const WaterComponent& now, const WaterComponent& saved) {
						return now.size == saved.size && now.cellSize == saved.cellSize && now.sprayDroplets == saved.sprayDroplets;
					},
					[](const WaterComponent& now, WaterComponent& value) {
						value.placed = now.placed;
						value.surface = now.surface;
						value.spraySurface = now.spraySurface;
						value.hasSpray = now.hasSpray;
						value.simulation = now.simulation;
						value.placedMatrix = now.placedMatrix;
					}));
				// Meshes: only whether they show (anything else about them needs a rebuild anyway)
				snapshot->restores.push_back(SnapshotOf<MeshComponent>(graph,
					[](const MeshComponent& now, const MeshComponent& saved) {
						return now.type == saved.type && now.file == saved.file && now.displacement == saved.displacement
							&& now.tree == saved.tree && now.radius == saved.radius;
					},
					[](const MeshComponent& now, MeshComponent& value) {
						const bool enabled = value.enabled;
						value = now;
						value.enabled = enabled;
					}));
				playScene = std::move(snapshot);
			}

			void MainRayTracingScene::RestorePlaySnapshot() {
				if (!playScene) {
					return;
				}
				PlaySnapshot& snapshot = *playScene;

				// The quick way when the same entities are there with the same kinds of components, and nothing
				// needed a rebuild meanwhile
				bool quick = rebuildCount == snapshot.rebuilds && !structureChanged && !treesChanged && graph.DepthFirst() == snapshot.order;
				for (const auto& restore : snapshot.restores) {
					quick = quick && restore(false);
				}

				const Environment& before = snapshot.environment;
				const bool groundChanged = before.ground != environment.ground || before.terrain != environment.terrain
					|| before.groundHeight != environment.groundHeight || before.groundMaterial.color != environment.groundMaterial.color
					|| before.groundMaterial.roughness != environment.groundMaterial.roughness;
				environment = before;

				if (quick) {
					for (const auto& restore : snapshot.restores) {
						restore(true);
					}
					graph.UpdateTransforms();
					if (groundChanged) {
						structureChanged = true;
					}
					std::cout << "Play: stopped, the scene is back as it was\n";
					return;
				}

				// Otherwise the whole scene again, from the snapshot (one rebuild; its trees are the same trees)
				Fox::Scene::SceneGraph restored;
				try {
					TreeAssets assets;
					assets.loaded = snapshot.trees;
					treeAssets = &assets;
					EntitiesFromJson(json::parse(snapshot.entities), restored);
					treeAssets = nullptr;
				}
				catch (const std::exception& e) {
					treeAssets = nullptr;
					std::cout << "Play: could not restore the scene (" << e.what() << "): it stays as it is\n";
					return;
				}
				graph = std::move(restored);
				objectEntities.clear();
				structureChanged = true;
				treesChanged = true;
				std::cout << "Play: stopped, the scene is back as it was (rebuilt: entities or meshes changed while it played)\n";
			}

			bool MainRayTracingScene::StartPlay(std::string& error) {
				if (physicsPlaying) {
					return true;
				}
				clock = GameClock{};
				TakePlaySnapshot();
				if (!StartPhysics(error)) {
					playScene.reset();
					return false;
				}
				std::cout << "Play: started\n";
				return true;
			}

			void MainRayTracingScene::StopPlay(bool restore) {
				if (!physicsPlaying) {
					playScene.reset();
					return;
				}
				StopPhysics(); // Bodies out, entities back where they were
				if (restore) {
					RestorePlaySnapshot();
				}
				playScene.reset();
				clock = GameClock{};
			}

			bool MainRayTracingScene::LoadFromFile(const std::string& path, CameraPose& camera, std::string& error) {
				StopPlay(false);
				json file;
				try {
					std::ifstream in(path);
					if (!in) {
						error = "Cannot read " + path;
						return false;
					}
					file = json::parse(in, nullptr, true, true); // Comments allowed
				}
				catch (const std::exception& e) {
					error = path + ": " + e.what();
					return false;
				}

				// Read everything first: a broken file leaves the current scene as it is
				Fox::Scene::SceneGraph newGraph;
				Environment newEnvironment;
				CameraPose newCamera;
				std::vector<Fox::Scene::CameraPath> newPaths;
				int newActivePath = 0;
				Fox::Scene::Terrain newTerrain;
				try {
					if (file.contains("terrain")) {
						const json& entry = file.at("terrain");
						newTerrain.Create(entry.get<Fox::Scene::TerrainSettings>());
						const std::string heightsFile = entry.value("heights", std::string());
						const std::string blendFile = entry.value("blend", std::string());
						if (!heightsFile.empty() && !newTerrain.LoadHeights(heightsFile, error)) {
							return false;
						}
						if (!blendFile.empty() && !newTerrain.LoadBlend(blendFile, error)) {
							return false;
						}
					}
					newPaths = file.value("cameraPaths", std::vector<Fox::Scene::CameraPath>{});
					newActivePath = file.value("activeCameraPath", 0);
					newCamera = file.value("camera", CameraPose{});
					newEnvironment = file.value("environment", Environment{});

					if (file.contains("entities")) {
						TreeAssets assets;
						for (const json& entry : file.value("treeAssets", json::array())) {
							assets.loaded.push_back(TreeFromJson(entry));
						}
						treeAssets = &assets;
						try {
							EntitiesFromJson(file.at("entities"), newGraph);
						}
						catch (...) {
							treeAssets = nullptr;
							throw;
						}
						treeAssets = nullptr;
					}
					else {
						Version1FromJson(file, newGraph);
					}
					if (file.value("version", 1) < 3) {
						SunFromEnvironment(file.value("environment", json()), newGraph);
					}
				}
				catch (const std::exception& e) {
					error = path + ": " + e.what();
					return false;
				}

				graph = std::move(newGraph);
				objectEntities.clear();
				environment = newEnvironment;
				ResetTerrain();
				if (newTerrain.IsCreated()) {
					newTerrain.blendTexture = terrain.blendTexture; // Reused when the size matches
					newTerrain.blendTextureResolution = terrain.blendTextureResolution;
					terrain = std::move(newTerrain);
					terrain.MarkAllDirty();
				}
				cameraManager.Clear();
				cameraManager.GetPaths() = std::move(newPaths);
				cameraManager.SetActivePath(newActivePath);
				camera = newCamera;
				pendingCamera = newCamera; // The renderer moves the camera there (TakePendingCamera)
				hasPendingCamera = true;
				structureChanged = true;
				treesChanged = true;
				return true;
			}
		}
	}
}
