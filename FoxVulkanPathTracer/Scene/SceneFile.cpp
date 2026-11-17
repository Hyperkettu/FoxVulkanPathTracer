// Scene files: the content of MainRayTracingScene as JSON (Scenes/*.json).
//
// {
//   "version": 1,
//   "camera":      { "position": [x, y, z], "target": [x, y, z] },
//   "environment": { "ground": true, "groundHeight": -3.73, "groundMaterial": {...}, "sun": true, ... },
//   "models":  [ { "name": "Cabin", "file": "Models/cabin.glb", "displacement": true, "transform": {...} } ],
//   "spheres": [ { "name": "Glass ball", "radius": 1.2, "material": {...}, "transform": {...} } ],
//   "trees":   [ { "name": "Tree 1", "transform": {...}, "shape": {...}, "look": {...}, "wind": {...} } ],
//   "cameraPaths": [ { "name": "Path 1", "speed": 4, "ease": 1.5, "loop": false, "lookAlongPath": false,
//                      "keys": [ { "position": [x, y, z], "target": [x, y, z] }, ... ] } ],
//   "activeCameraPath": 0
// }
//
// A transform is { "position": [x, y, z], "rotation": [x, y, z] (Euler angles, degrees), "scale": [x, y, z] }.
// Every value is optional: what a file leaves out keeps its default, so files stay readable by later versions.

#include "FoxRenderer.h"
#include "FoxVulkanPathTracer/Core/TinyGLTF/json.hpp"

#include <charconv>
#include <filesystem>

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
		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CameraPath, name, speed, ease, loop, lookAlongPath, keys)

		namespace RayTracing {

			using Scene = MainRayTracingScene;

			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene::SurfaceMaterial,
				color, roughness, metallic, transmission, ior, attenuationColor, attenuationDistance)
			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene::SurfaceTextures,
				albedo, normal, roughness, metallic, occlusion, displacement, normalGreenUp, invertDisplacement)
			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene::TreeLook,
				bark, leaf, barkTint, leafTint, barkRoughness, leafRoughness, alphaCutoff)
			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene::Environment,
				ground, groundHeight, groundMaterial, sun, sunDirection, sunColor, sunIntensity, sunAngularRadius)
			NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene::CameraPose, position, target)

			namespace {
				const int FILE_VERSION = 1;

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
			}

			bool MainRayTracingScene::SaveToFile(const std::string& path, const CameraPose& camera, std::string& error) {
				SyncTransforms(); // Where the gizmos left everything

				json file;
				file["version"] = FILE_VERSION;
				file["camera"] = camera;
				file["environment"] = environment;
				file["cameraPaths"] = cameraManager.GetPaths();
				file["activeCameraPath"] = cameraManager.GetActivePath();

				file["models"] = json::array();
				for (const Model& model : models) {
					file["models"].push_back({
						{ "name", model.name },
						{ "file", model.file },
						{ "displacement", model.displacement },
						{ "transform", TransformToJson(model.transform) },
					});
				}

				file["spheres"] = json::array();
				for (const Sphere& sphere : spheres) {
					file["spheres"].push_back({
						{ "name", sphere.name },
						{ "radius", sphere.radius },
						{ "material", sphere.material },
						{ "transform", TransformToJson(sphere.transform) },
					});
				}

				file["trees"] = json::array();
				for (const Tree& tree : trees) {
					file["trees"].push_back({
						{ "name", tree.name },
						{ "transform", TransformToJson(tree.transform) },
						{ "shape", tree.shape },
						{ "look", tree.look },
						{ "wind", tree.wind },
					});
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
				std::vector<Model> newModels;
				std::vector<Sphere> newSpheres;
				std::vector<Tree> newTrees;
				Environment newEnvironment;
				CameraPose newCamera;
				std::vector<Fox::Scene::CameraPath> newPaths;
				int newActivePath = 0;
				try {
					newPaths = file.value("cameraPaths", std::vector<Fox::Scene::CameraPath>{});
					newActivePath = file.value("activeCameraPath", 0);
					newCamera = file.value("camera", CameraPose{});
					newEnvironment = file.value("environment", Environment{});

					for (const json& entry : file.value("models", json::array())) {
						Model model;
						model.file = entry.at("file").get<std::string>();
						model.name = entry.value("name", std::filesystem::path(model.file).stem().string());
						model.displacement = entry.value("displacement", false);
						model.transform = TransformFromJson(entry.value("transform", json::object()));
						newModels.push_back(std::move(model));
					}

					for (const json& entry : file.value("spheres", json::array())) {
						Sphere sphere;
						sphere.name = entry.value("name", "Sphere " + std::to_string(newSpheres.size() + 1));
						sphere.radius = entry.value("radius", 1.0f);
						sphere.material = entry.value("material", SurfaceMaterial{});
						sphere.transform = TransformFromJson(entry.value("transform", json::object()));
						newSpheres.push_back(std::move(sphere));
					}

					for (const json& entry : file.value("trees", json::array())) {
						Tree tree;
						tree.name = entry.value("name", "Tree " + std::to_string(newTrees.size() + 1));
						tree.transform = TransformFromJson(entry.value("transform", json::object()));
						tree.shape = entry.value("shape", Fox::Graphics::Geometry::TreeSettings{});
						tree.look = entry.value("look", TreeLook{});
						tree.wind = entry.value("wind", Fox::Animation::WindSkeleton::Settings{});
						newTrees.push_back(std::move(tree));
					}
				}
				catch (const std::exception& e) {
					error = path + ": " + e.what();
					return false;
				}

				models = std::move(newModels);
				spheres = std::move(newSpheres);
				trees = std::move(newTrees);
				environment = newEnvironment;
				cameraManager.Clear();
				cameraManager.GetPaths() = std::move(newPaths);
				cameraManager.SetActivePath(newActivePath);
				camera = newCamera;
				pendingCamera = newCamera; // The renderer moves the camera there (TakePendingCamera)
				hasPendingCamera = true;
				structureChanged = true;
				return true;
			}
		}
	}
}
