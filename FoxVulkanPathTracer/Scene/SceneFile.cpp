// Scene files: the content of MainRayTracingScene as JSON (Scenes/*.json).
//
// {
//   "version": 2,
//   "camera":      { "position": [x, y, z], "target": [x, y, z] },
//   "environment": { "ground": true, "groundHeight": -3.73, "groundMaterial": {...}, "sun": true, ... },
//   "entities": [
//     { "name": "Cabin", "active": true, "parent": -1,
//       "components": { "transform": {...}, "mesh": { "type": "model", "file": "Models/cabin.glb", "displacement": true } } },
//     { "name": "Glass ball", "parent": 0,
//       "components": { "transform": {...}, "mesh": { "type": "sphere", "radius": 1.2, "material": {...} } } },
//     { "name": "Tree 1", "components": { "transform": {...}, "mesh": { "type": "tree", "shape": {...}, "look": {...}, "wind": {...} } } }
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
				ground, terrain, groundHeight, groundMaterial, sun, sunDirection, sunColor, sunIntensity, sunAngularRadius)
			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene::CameraPose, position, target)

			namespace {
				const int FILE_VERSION = 2;

				using Fox::ECS::Entity;
				using Fox::Scene::SceneGraph;
				using Fox::Scene::MeshComponent;
				using Fox::Scene::TransformComponent;

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
						if (mesh.tree) {
							j["shape"] = mesh.tree->shape;
							j["look"] = mesh.tree->look;
							j["wind"] = mesh.tree->wind;
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
						mesh.tree = std::make_shared<Fox::Scene::Tree>();
						mesh.tree->shape = j.value("shape", Fox::Graphics::Geometry::TreeSettings{});
						mesh.tree->look = j.value("look", Fox::Scene::TreeLook{});
						mesh.tree->wind = j.value("wind", Fox::Animation::WindSkeleton::Settings{});
					}
					else {
						mesh.type = MeshComponent::Type::Model;
						mesh.file = j.value("file", std::string());
						mesh.displacement = j.value("displacement", false);
					}
					return mesh;
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
				};

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
				file["entities"] = EntitiesToJson(graph);

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
						EntitiesFromJson(file.at("entities"), newGraph);
					}
					else {
						Version1FromJson(file, newGraph);
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
