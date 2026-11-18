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
	}

	namespace Scene {
		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CameraKey, position, target)
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

			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene::Environment,
				ground, terrain, groundHeight, groundMaterial)
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
					return { { "shape", tree.shape }, { "look", tree.look }, { "wind", tree.wind } };
				}

				std::shared_ptr<Fox::Scene::Tree> TreeFromJson(const json& j) {
					auto tree = std::make_shared<Fox::Scene::Tree>();
					tree->shape = j.value("shape", Fox::Graphics::Geometry::TreeSettings{});
					tree->look = j.value("look", Fox::Scene::TreeLook{});
					tree->wind = j.value("wind", Fox::Animation::WindSkeleton::Settings{});
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
				// While physics plays, the scene is saved as it was before it started
				struct PutBack {
					Fox::Scene::SceneGraph& graph;
					std::vector<std::pair<Entity, Fox::Scene::Transform>> transforms;
					~PutBack() {
						for (const auto& [entity, transform] : transforms) {
							graph.SetLocalTransform(entity, transform);
						}
						if (!transforms.empty()) {
							graph.UpdateTransforms();
						}
					}
				} putBack{ graph, {} };
				if (physicsPlaying) {
					for (const auto& [entity, transform] : playSnapshot) {
						if (graph.IsValid(entity) && graph.HasTransform(entity)) {
							putBack.transforms.emplace_back(entity, graph.GetLocalTransform(entity));
							graph.SetLocalTransform(entity, transform);
						}
					}
					graph.UpdateTransforms();
				}

				json file;
				file["version"] = FILE_VERSION;
				file["camera"] = camera;
				file["environment"] = environment;
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
				{
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

			bool MainRayTracingScene::LoadFromFile(const std::string& path, CameraPose& camera, std::string& error) {
				StopPhysics();
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
