#include "FoxRenderer.h"

#include <filesystem>
#include <random>
#include <unordered_set>
#include <unordered_map>

namespace Fox {

	namespace Scene {

			Fox::Graphics::Vulkan::Material SurfaceMaterial::ToMaterial() const {
				Fox::Graphics::Vulkan::Material material;
				material.baseColor[0] = color.r;
				material.baseColor[1] = color.g;
				material.baseColor[2] = color.b;
				material.roughness = roughness;
				material.metallic = metallic;
				material.transmission = transmission;
				if (transmission > 0.0f && ior > 0.0f) {
					material.ior = ior;
					const float attenuation[3] = { attenuationColor.r, attenuationColor.g, attenuationColor.b };
					material.SetAttenuation(attenuation, attenuationDistance);
				}
				const float strength = std::max(emissionStrength, 0.0f);
				for (int c = 0; c < 3; c++) {
					material.emissive[c] = std::max(emission[c], 0.0f) * strength;
				}
				return material;
			}

		namespace RayTracing {

			using Fox::Scene::MeshComponent;
			using Fox::Scene::LightComponent;

			void MainRayTracingScene::Initialize() {
				textureLibrary = std::make_unique<Fox::Scene::TextureLibrary>(device, physicalDevice, commandPool->Get(), queue);

				// The scene comes from its file; without one it starts empty (ground and sun only)
				std::string error;
				if (std::filesystem::exists(DEFAULT_SCENE_FILE)) {
					CameraPose camera;
					if (!LoadFromFile(DEFAULT_SCENE_FILE, camera, error)) {
						std::cout << "Scene: " << error << "\n";
					}
				}
				else {
					CreateSun(graph);
					std::cout << "Scene: no " << DEFAULT_SCENE_FILE << ", starting with an empty scene\n";
				}

				// Sound, then C# scripts (Scripts/*.cs)
				audio = std::make_unique<Fox::Audio::AudioEngine>();
				InitializeScripts();

				if (!RebuildScene(error)) {
					std::cout << "Scene: " << error << "\n";
				}
			}

			Fox::Graphics::Geometry::Vulkan::Mesh& MainRayTracingScene::LoadModel(const std::string& file, bool displacement) {
				const std::string key = file + (displacement ? "|displaced" : "");
				if (auto it = loadedModels.find(key); it != loadedModels.end()) {
					return *it->second;
				}

				// Displacement: glTF has no height maps, so the relief is reconstructed from the normal maps (see
				// Graphics/Geometry/Displacement.h). Default budget: up to 4 M triangles per model.
				Fox::Graphics::Geometry::DisplacementSettings settings;
				settings.enabled = displacement;

				auto mesh = std::make_unique<Fox::Graphics::Geometry::Vulkan::Mesh>(device, physicalDevice, queue, queueFamily);
				mesh->LoadFromGLTF(file, settings);
				return *(loadedModels[key] = std::move(mesh));
			}

			bool MainRayTracingScene::RebuildScene(std::string& error) {
				rebuildCount++;
				graph.UpdateTransforms();
				Clear();
				objectEntities.clear();
				std::string problems;

				auto& registry = graph.GetRegistry();
				const std::vector<Entity> entities = graph.DepthFirst();
				auto placed = [&](Entity entity, MeshComponent& mesh, const glm::mat4& world) {
					mesh.object = GetObjectCount() - 1;
					mesh.placed = true;
					mesh.placedMatrix = world;
					mesh.shown = true;
					SetObjectTransform(mesh.object, graph.GetWorldTransform(entity)); // Exactly, when it can be
					objectEntities.resize(GetObjectCount());
					objectEntities[mesh.object] = entity;
				};

				// Static content: models and spheres, then the ground and sun. A placement can take up to three top
				// level instances (one per surface kind); half the room goes to them, the rest to trees.
				const size_t staticBudget = 8'192; // The rest: trees and clutter
				size_t leftOut = 0;
				for (Entity entity : entities) {
					MeshComponent* mesh = registry.TryGet<MeshComponent>(entity);
					if (!mesh) {
						continue;
					}
					mesh->placed = false;
					if (mesh->type != MeshComponent::Type::Tree && GetObjectCount() >= staticBudget) {
						leftOut++;
						continue;
					}
					const glm::mat4 world = registry.Get<Fox::Scene::WorldTransformComponent>(entity).matrix;
					const std::string& name = graph.GetName(entity);

					if (mesh->type == MeshComponent::Type::Model) {
						if (mesh->file.empty()) {
							continue;
						}
						try {
							AddMesh(LoadModel(mesh->file, mesh->displacement), world, name);
							placed(entity, *mesh, world);
						}
						catch (const std::exception& e) {
							problems += std::string(problems.empty() ? "" : "; ") + mesh->file + ": " + e.what();
						}
					}
					else if (mesh->type == MeshComponent::Type::Sphere) {
						Fox::Graphics::Vulkan::Material material = mesh->material.ToMaterial();
						if (!mesh->material.emissionMap.empty() && mesh->material.emissionStrength > 0.0f) {
							material.emissiveTexture = textureLibrary->Texture(mesh->material.emissionMap, true);
						}
						AddSphere(glm::vec3(0.0f), mesh->radius, material, name);
						placed(entity, *mesh, world);
					}
				}

				if (leftOut > 0) {
					problems += std::string(problems.empty() ? "" : "; ") + std::to_string(leftOut) + " models and spheres left out (at most "
						+ std::to_string(staticBudget) + ")";
				}

				terrainPlaced = false;
				if (environment.terrain) {
					PlaceTerrain();
				}
				else if (environment.ground) {
					AddGroundPlane(environment.groundHeight, 2000.0f, environment.groundMaterial.ToMaterial());
				}

				// Clutter: on the ground just placed; particle effects
				PlaceClutter(entities, problems);
				PlaceParticles(entities, problems);
				PlaceTexts(entities, problems);

				// Lights: one per entity with a light, dark while it is off (so turning it on needs no rebuild)
				for (Entity entity : entities) {
					LightComponent* light = registry.TryGet<LightComponent>(entity);
					if (!light) {
						continue;
					}
					light->uploaded = light->ToLight(registry.Get<Fox::Scene::WorldTransformComponent>(entity).matrix,
						light->enabled && graph.IsActiveInHierarchy(entity));
					light->index = RayTracingScene::AddLight(light->uploaded);
					light->placed = true;
				}
				// Water: its surface (written by its simulation every frame), placed now; the simulations need the
				// scene's buffers, made by Build
				pendingWater.clear();
				for (Entity entity : entities) {
					if (Fox::Scene::WaterComponent* water = registry.TryGet<Fox::Scene::WaterComponent>(entity)) {
						water->placed = false;
						water->simulation = nullptr;
						if (water->enabled && graph.IsActiveInHierarchy(entity)) {
							PlaceWater(entity, *water);
						}
					}
				}
				objectEntities.resize(GetObjectCount());

				Build();
				structureChanged = false;

				for (PendingWater& pending : pendingWater) {
					Fox::Scene::WaterComponent& water = registry.Get<Fox::Scene::WaterComponent>(pending.entity);
					const SimulatedRange range = GetSimulatedRange(water.surface);
					const SimulatedRange spray = water.hasSpray ? GetSimulatedRange(water.spraySurface) : SimulatedRange{};
					water.simulation = std::make_shared<Fox::Scene::WaterSimulation>(device, physicalDevice, commandPool->Get(), queue,
						pending.grid, pending.bed, pending.depth, range.firstVertex, range.firstPrevious,
						GetVertexStorageBuffer(), GetBuildVertexBuffer(), GetPreviousPositionsBuffer(),
						pending.droplets, spray.firstVertex, spray.firstPrevious);
					water.placed = true;
				}
				pendingWater.clear();

				// Dynamic content: trees. Their objects went with the rest; until they are placed again they have none.
				registry.Each<MeshComponent>([](Entity, MeshComponent& mesh) {
					if (mesh.type == MeshComponent::Type::Tree) {
						mesh.placed = false;
						if (mesh.tree) {
							mesh.tree->placed = false;
						}
					}
				});
				std::string treeError;
				if (!ApplyTreeChanges(treeError)) {
					problems += std::string(problems.empty() ? "" : "; ") + treeError;
				}

				// Hidden entities stay placed, out of sight
				ForgetStalePlacements();
				SyncMeshes();
				objectsChanged = true;

				error = problems;
				return problems.empty();
			}

			void MainRayTracingScene::SyncLights() {
				auto& registry = graph.GetRegistry();
				registry.Each<LightComponent>([&](Entity entity, LightComponent& light) {
					if (!light.placed) {
						return;
					}
					const Fox::Graphics::Vulkan::Light now = light.ToLight(registry.Get<Fox::Scene::WorldTransformComponent>(entity).matrix,
						light.enabled && graph.IsActiveInHierarchy(entity));
					if (!LightComponent::Same(now, light.uploaded)) {
						SetLight(light.index, now);
						light.uploaded = now;
						objectsChanged = true;
					}
				});
			}

			glm::quat MainRayTracingScene::RotationTowards(const glm::vec3& towardsLight) {
				const glm::vec3 direction = glm::dot(towardsLight, towardsLight) > 1e-12f ? glm::normalize(towardsLight) : glm::vec3(0.0f, 1.0f, 0.0f);
				return glm::normalize(glm::rotation(glm::vec3(0.0f, 0.0f, 1.0f), direction));
			}

			MainRayTracingScene::Entity MainRayTracingScene::CreateSun(Fox::Scene::SceneGraph& graph, const Sun& sun) {
				const Entity entity = graph.CreateEntity("Sun");
				Fox::Scene::Transform transform;
				transform.position = glm::vec3(0.0f, 20.0f, 0.0f); // Out of the way: a directional light's position does not matter
				transform.rotation = RotationTowards(sun.direction);
				graph.SetLocalTransform(entity, transform);

				LightComponent light;
				light.type = LightComponent::Type::Directional;
				light.color = sun.color;
				light.intensity = sun.intensity;
				light.angularRadius = sun.angularRadius;
				graph.GetRegistry().Add<LightComponent>(entity, std::move(light));
				return entity;
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddLight(LightComponent::Type type, const glm::vec3& position, Entity parent) {
				const Entity entity = graph.CreateEntity(LightComponent::TypeName(type), parent);
				Fox::Scene::Transform transform;
				transform.position = position;
				transform.rotation = RotationTowards(glm::vec3(0.0f, 1.0f, 0.0f)); // Shining down
				graph.SetWorldTransform(entity, transform);
				AddLightComponent(entity, type);
				return entity;
			}

			LightComponent& MainRayTracingScene::AddLightComponent(Entity entity, LightComponent::Type type) {
				RemoveLightComponent(entity);
				LightComponent light;
				light.type = type;
				if (type != LightComponent::Type::Directional) {
					light.color = glm::vec3(1.0f, 0.85f, 0.65f); // Warm, like a bulb
					light.intensity = 50.0f;
					light.radius = 0.05f;
				}
				structureChanged = true; // Room for it in the light buffer
				return graph.GetRegistry().Add<LightComponent>(entity, std::move(light));
			}

			void MainRayTracingScene::RemoveLightComponent(Entity entity) {
				LightComponent* light = graph.GetRegistry().TryGet<LightComponent>(entity);
				if (!light) {
					return;
				}
				if (light->placed) {
					Fox::Graphics::Vulkan::Light dark = light->uploaded;
					dark.color = glm::vec3(0.0f); // Dark until the rebuild takes it out
					SetLight(light->index, dark);
					objectsChanged = true;
				}
				graph.GetRegistry().Remove<LightComponent>(entity);
				structureChanged = true;
			}

			void MainRayTracingScene::ForgetStalePlacements() {
				graph.GetRegistry().Each<MeshComponent>([&](Entity entity, MeshComponent& mesh) {
					if (mesh.placed && (mesh.object >= objectEntities.size() || objectEntities[mesh.object] != entity)) {
						mesh.placed = false;
						if (mesh.tree) {
							mesh.tree->placed = false;
						}
					}
				});
			}

			void MainRayTracingScene::SyncMeshes() {
				auto& registry = graph.GetRegistry();
				registry.Each<MeshComponent>([&](Entity entity, MeshComponent& mesh) {
					if (!mesh.placed || mesh.object >= GetObjectCount()) {
						return;
					}
					const glm::mat4& world = registry.Get<Fox::Scene::WorldTransformComponent>(entity).matrix;
					if (world != mesh.placedMatrix) {
						SetObjectTransform(mesh.object, graph.GetWorldTransform(entity));
						mesh.placedMatrix = world;
						objectsChanged = true;
						meshesMoved = meshesMoved || !IsNavAgentPart(entity);
					}
					const bool show = mesh.enabled && graph.IsActiveInHierarchy(entity);
					if (show != IsObjectVisible(mesh.object)) {
						SetObjectVisible(mesh.object, show);
						objectsChanged = true;
						meshesMoved = meshesMoved || !IsNavAgentPart(entity);
					}
					mesh.shown = show;
				});
			}

			void MainRayTracingScene::ClearContent() {
				StopPlay(false);
				graph.Clear();
				CreateSun(graph);
				environment = Environment{};
				cameraManager.Clear();
				ResetTerrain();
				structureChanged = true;
				treesChanged = true;
			}

			MainRayTracingScene::Entity MainRayTracingScene::CreateEmpty(const std::string& name, Entity parent) {
				return graph.CreateEntity(name, parent);
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddModel(const std::string& file, bool displacement, const glm::vec3& position, Entity parent) {
				const Entity entity = graph.CreateEntity(std::filesystem::path(file).stem().string(), parent);
				Fox::Scene::Transform transform;
				transform.position = position;
				graph.SetWorldTransform(entity, transform);

				MeshComponent mesh;
				mesh.type = MeshComponent::Type::Model;
				mesh.file = file;
				mesh.displacement = displacement;
				MeshChanged(&graph.GetRegistry().Add<MeshComponent>(entity, std::move(mesh)), nullptr);
				return entity;
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddSphereObject(const glm::vec3& position, float radius, const SurfaceMaterial& material, Entity parent) {
				size_t spheres = 0;
				graph.GetRegistry().Each<MeshComponent>([&](Entity, MeshComponent& mesh) {
					spheres += mesh.type == MeshComponent::Type::Sphere ? 1 : 0;
				});
				const Entity entity = graph.CreateEntity("Sphere " + std::to_string(spheres + 1), parent);
				Fox::Scene::Transform transform;
				transform.position = position;
				graph.SetWorldTransform(entity, transform);

				MeshComponent mesh;
				mesh.type = MeshComponent::Type::Sphere;
				mesh.radius = radius;
				mesh.material = material;
				MeshChanged(&graph.GetRegistry().Add<MeshComponent>(entity, std::move(mesh)), nullptr);
				return entity;
			}

			MeshComponent& MainRayTracingScene::AddMeshComponent(Entity entity, MeshComponent::Type type) {
				MeshComponent mesh;
				mesh.type = type;
				if (type == MeshComponent::Type::Tree) {
					mesh.tree = std::make_shared<Tree>();
				}
				else if (type == MeshComponent::Type::Sphere) {
					mesh.material.color = glm::vec3(0.85f);
					mesh.material.roughness = 0.4f;
				}
				RemoveMeshComponent(entity);
				MeshComponent& added = graph.GetRegistry().Add<MeshComponent>(entity, std::move(mesh));
				MeshChanged(&added, nullptr);
				return added;
			}

			void MainRayTracingScene::RemoveMeshComponent(Entity entity) {
				MeshComponent* mesh = graph.GetRegistry().TryGet<MeshComponent>(entity);
				if (!mesh) {
					return;
				}
				if (mesh->placed && mesh->object < GetObjectCount()) {
					SetObjectVisible(mesh->object, false); // Out of sight until the rebuild takes it out
				}
				const MeshComponent::Type type = mesh->type;
				graph.GetRegistry().Remove<MeshComponent>(entity);
				MeshChanged(nullptr, &type);
			}

			void MainRayTracingScene::MeshChanged(const MeshComponent* now, const MeshComponent::Type* previous) {
				auto affects = [&](MeshComponent::Type type) {
					if (type == MeshComponent::Type::Tree) {
						treesChanged = true;
					}
					else {
						structureChanged = true;
					}
				};
				if (now) {
					affects(now->type);
				}
				if (previous) {
					affects(*previous);
				}
			}

			void MainRayTracingScene::DestroyEntity(Entity entity) {
				if (!graph.IsValid(entity)) {
					return;
				}
				auto& registry = graph.GetRegistry();
				for (Entity e : graph.Subtree(entity)) {
					if (MeshComponent* mesh = registry.TryGet<MeshComponent>(e)) {
						// Out of sight right away; out of the scene with the rebuild
						if (mesh->placed && mesh->object < GetObjectCount()) {
							SetObjectVisible(mesh->object, false);
						}
						const MeshComponent::Type type = mesh->type;
						MeshChanged(nullptr, &type);
					}
					RemoveLightComponent(e);
					if (Fox::Scene::WaterComponent* water = registry.TryGet<Fox::Scene::WaterComponent>(e); water && water->placed) {
						SetObjectVisible(GetSurfaceObject(water->surface), false); // Out of sight until the rebuild takes it out
						if (water->hasSpray) {
							SetObjectVisible(GetSurfaceObject(water->spraySurface), false);
						}
						water->simulation = nullptr;
						water->placed = false;
						structureChanged = true;
					}
				}
				graph.DestroyEntity(entity);
			}

			MainRayTracingScene::Entity MainRayTracingScene::DuplicateEntity(Entity entity, bool instanceTrees) {
				const Entity copy = graph.Duplicate(entity);
				if (!graph.IsValid(copy)) {
					return copy;
				}
				auto& registry = graph.GetRegistry();
				for (Entity e : graph.Subtree(copy)) {
					if (MeshComponent* mesh = registry.TryGet<MeshComponent>(e)) {
						// A placement of its own; a tree of its own too, unless it is an instance of the same tree
						mesh->placed = false;
						if (mesh->tree && !instanceTrees) {
							mesh->tree = mesh->tree->CloneSettings();
						}
						MeshChanged(mesh, nullptr);
					}
					if (LightComponent* light = registry.TryGet<LightComponent>(e)) {
						light->placed = false; // A light of its own, with the rebuild
						structureChanged = true;
					}
					if (Fox::Scene::WaterComponent* water = registry.TryGet<Fox::Scene::WaterComponent>(e)) {
						water->placed = false; // Water of its own, with the rebuild
						water->simulation = nullptr;
						structureChanged = true;
					}
				}
				graph.SetName(copy, graph.GetName(copy) + " (copy)");
				return copy;
			}

			MainRayTracingScene::Entity MainRayTracingScene::GetEntityOfObject(int object) const {
				if (object < 0 || static_cast<size_t>(object) >= objectEntities.size()) {
					return {};
				}
				const Entity entity = objectEntities[static_cast<size_t>(object)];
				return graph.IsValid(entity) ? entity : Entity{};
			}

			std::vector<MainRayTracingScene::Entity> MainRayTracingScene::GetTreeEntities() const {
				std::vector<Entity> result;
				for (Entity entity : graph.DepthFirst()) {
					const MeshComponent* mesh = graph.GetRegistry().TryGet<MeshComponent>(entity);
					if (mesh && mesh->type == MeshComponent::Type::Tree && mesh->tree) {
						result.push_back(entity);
					}
				}
				return result;
			}

			MainRayTracingScene::Tree* MainRayTracingScene::GetTree(Entity entity) {
				MeshComponent* mesh = graph.GetRegistry().TryGet<MeshComponent>(entity);
				return mesh && mesh->type == MeshComponent::Type::Tree ? mesh->tree.get() : nullptr;
			}

			float MainRayTracingScene::AdvanceClock(float realDelta) {
				float step = 0.0f;
				if (!clock.paused) {
					step = std::min(realDelta, MAX_GAME_STEP) * clock.timeScale;
				}
				else if (clock.stepFrames > 0) {
					clock.stepFrames--;
					step = STEP_FRAME;
				}
				clock.delta = step;
				if (step > 0.0f) {
					clock.time += step;
					clock.frame++;
				}
				return step;
			}

			void MainRayTracingScene::Update(float dt) {
				// While playing everything moves on game time (paused: not at all); in edit mode on real time (water,
				// wind, growing trees)
				const float step = physicsPlaying ? AdvanceClock(dt) : dt;
				time += step;

				// The systems, in order:
				//   1. transforms        world matrices from the hierarchy (and the gizmos' edits)
				//   1b. scripts          C# OnStart / OnUpdate (playing)
				//   2. agents            walk their paths (edit mode: plan them)
				//   3. physics           fixed steps; dynamic bodies move their entities, kinematic ones follow
				//   4. render sync       meshes, lights and water placed as their entities are now
				//   5. animation         water simulation, wind in the trees, growing trees
				graph.UpdateTransforms();
				if (scripts && scripts->Poll(dt)) {
					// Hot reload: on real time, also while paused. Scripts whose class was missing try again.
					graph.GetRegistry().Each<Fox::Scene::ScriptComponent>([](Entity, Fox::Scene::ScriptComponent& component) {
						for (auto& entry : component.scripts) {
							if (entry.instance < 0) {
								entry.instance = 0;
							}
						}
					});
				}
				if (physicsPlaying) {
					UpdateScripts(step);
					graph.UpdateTransforms();
				}
				UpdateNavigation(step);
				if (physicsPlaying) {
					graph.UpdateTransforms();
					UpdatePhysics(step);
					graph.UpdateTransforms();
				}
				SyncMeshes();
				UpdateClutter(dt);
				UpdateParticles(step);
				UpdateTexts();
				SyncLights();
				UpdateAudio();
				UpdateWater(step);

				// Wind in every tree; it blows in world space, wherever a tree has been moved to. Instances of a tree
				// share its deformation: it is computed once, for its first placement.
				//
				// Level of detail: the wind is worth its cost (the skeleton here; on the GPU skinning every vertex and the
				// tree's acceleration structures) only where it shows. By its size on screen (pixels, its nearest
				// instance): every frame (600+), every 2nd (150+), every 4th (40+; with the time missed: the springs take
				// substeps), or standing still. Branches sway at a hertz or two, so even every 4th frame looks smooth.
				// Updates of the less frequent ones are spread over the frames; in between their shape is held (and
				// has no motion of its own, so the denoiser sees it still).
				std::unordered_set<const Tree*> blown;
				windStats = WindStats{};
				windFrame++;
				uint32_t treeIndex = 0;
				const auto& view = GetClusterLodView();
				const float windAngle = glm::radians(environment.windDirection);
				const glm::vec3 windDirection(std::cos(windAngle), 0.0f, std::sin(windAngle));
				// Instances tilt towards the wind about their base, around the horizontal axis across it
				const glm::vec3 leanAxis = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), windDirection));
				graph.GetRegistry().Each<MeshComponent>([&](Entity, MeshComponent& mesh) {
					Tree* tree = mesh.type == MeshComponent::Type::Tree ? mesh.tree.get() : nullptr;
					if (!tree || !tree->placed || !tree->skeleton || !mesh.placed || tree->handle.object >= GetObjectCount()
						|| !blown.insert(tree).second) {
						return;
					}
					// Growing: on to grown (or round again), unless paused
					if (tree->growth.enabled && !tree->growthPaused) {
						tree->growthTime += step;
						const float duration = std::max(tree->growth.duration, 0.01f);
						if (tree->growthTime > duration) {
							tree->growthTime = tree->growth.loop ? std::fmod(tree->growthTime, duration) : duration;
						}
					}
					tree->wind.windDirection = windDirection;

					// Every instance leans downwind on its own spring, every frame: only its traced transform changes
					std::vector<size_t> objects{ tree->handle.object };
					objects.insert(objects.end(), tree->handle.moreObjects.begin(), tree->handle.moreObjects.end());
					tree->leans.resize(objects.size());
					for (size_t i = 0; i < objects.size(); i++) {
						if (objects[i] >= GetObjectCount()) continue;
						const glm::vec3 base = GetObjectTransform(objects[i]).position;
						Fox::Animation::WindSkeleton::UpdateLean(tree->leans[i], tree->wind, base, time, step);
						const glm::mat4 sway = glm::translate(glm::mat4(1.0f), base) * glm::rotate(glm::mat4(1.0f), tree->leans[i].angle, leanAxis)
							* glm::translate(glm::mat4(1.0f), -base);
						SetObjectSway(objects[i], sway);
					}

					uint32_t interval = 1;
					if (windLevelOfDetail) {
						if (tree->size <= 0.0f && tree->mesh) {
							for (const auto& vertex : tree->mesh->vertices) {
								tree->size = std::max(tree->size, glm::length(glm::vec3(vertex.position[0], vertex.position[1], vertex.position[2])));
							}
						}
						float pixels = 0.0f;
						auto measure = [&](size_t object) {
							if (object >= GetObjectCount()) return;
							const ObjectTransform& placed = GetObjectTransform(object);
							const float scale = std::max({ placed.scale.x, placed.scale.y, placed.scale.z });
							const float distance = std::max(glm::distance(placed.position, view.cameraPosition), 0.1f);
							pixels = std::max(pixels, tree->size * scale * view.pixelScale / distance);
						};
						measure(tree->handle.object);
						for (size_t object : tree->handle.moreObjects) measure(object);
						interval = pixels >= 600.0f ? 1 : pixels >= 150.0f ? 2 : pixels >= 40.0f ? 4 : 0;
						const bool growing = tree->growth.enabled && !tree->growthPaused && tree->GrowthProgress() < 1.0f;
						if (interval == 0 && growing) {
							interval = 4; // Its shape changes: not still
						}
					}
					(interval == 1 ? windStats.every : interval == 2 ? windStats.half : interval == 4 ? windStats.quarter : windStats.still)++;

					tree->windPending += step;
					const bool due = interval > 0 && (windFrame + treeIndex++) % interval == 0;
					if (!due) {
						SetSkinUpdate(tree->handle.skinnedMesh, interval == 0 ? SkinUpdate::Freeze : SkinUpdate::Hold);
						return;
					}
					// At most a tenth of a second at once: a tree that stood still starts again calmly
					const float windStep = std::min(tree->windPending, 0.1f);
					tree->windPending = 0.0f;

					tree->skeleton->GetSettings() = tree->wind;
					tree->skeleton->SetGrowth(tree->GrowthProgress(), tree->growth);
					tree->skeleton->Update(windStep, time, GetObjectTransform(tree->handle.object).Matrix());
					SetJointMatrices(tree->handle.skinnedMesh, tree->skeleton->GetSkinMatrices());
					SetSkinGrowth(tree->handle.skinnedMesh, tree->skeleton->GetLeafFront(), tree->skeleton->GetLeafSpan());
					SetSkinUpdate(tree->handle.skinnedMesh, SkinUpdate::Deform);
				});
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddClutter(const glm::vec3& position) {
				const Entity entity = graph.CreateEntity("Grass");
				Fox::Scene::Transform transform;
				transform.position = position;
				graph.SetWorldTransform(entity, transform);
				graph.GetRegistry().Add<Fox::Scene::ClutterComponent>(entity);
				structureChanged = true;
				return entity;
			}

			void MainRayTracingScene::MakeGrassTuft(const Fox::Scene::ClutterComponent& clutter, std::vector<Fox::Graphics::Vulkan::Vertex>& vertices,
				std::vector<uint32_t>& indices) {
				// Blades: thin strips, widest at the base, bending out to one side, in a loose clump. Real geometry (no
				// alpha tested cards): opaque, so rays pass the gaps between blades at full speed.
				std::mt19937 rng(clutter.seed * 7919u + 17u);
				std::uniform_real_distribution<float> unit(0.0f, 1.0f);
				const float height = std::max(clutter.grassHeight, 0.01f);
				const int SEGMENTS = 4;
				for (int b = 0; b < std::clamp(clutter.blades, 1, 256); b++) {
					const float around = unit(rng) * glm::two_pi<float>();
					const float spread = std::sqrt(unit(rng)) * 0.25f * height;
					const glm::vec3 base(std::cos(around) * spread, 0.0f, std::sin(around) * spread);
					const float bladeHeight = height * (0.55f + 0.45f * unit(rng));
					const float width = height * 0.035f * (0.8f + 0.4f * unit(rng));
					const float facing = unit(rng) * glm::two_pi<float>();
					const glm::vec3 side(std::cos(facing), 0.0f, std::sin(facing));
					// Bends away from the clump's middle, more or less
					glm::vec3 out = glm::cross(side, glm::vec3(0.0f, 1.0f, 0.0f));
					if (glm::dot(out, base) < 0.0f) out = -out;
					const float bend = bladeHeight * (0.15f + 0.4f * unit(rng));

					const uint32_t first = static_cast<uint32_t>(vertices.size());
					for (int k = 0; k <= SEGMENTS; k++) {
						const float t = static_cast<float>(k) / SEGMENTS;
						const glm::vec3 centre = base + glm::vec3(0.0f, bladeHeight * t, 0.0f) + out * (bend * t * t);
						const glm::vec3 along = glm::normalize(glm::vec3(0.0f, bladeHeight, 0.0f) + out * (2.0f * bend * t));
						const glm::vec3 normal = glm::normalize(glm::cross(side, along));
						const float half = 0.5f * width * std::pow(1.0f - t, 0.8f);
						const int count = k < SEGMENTS ? 2 : 1;
						for (int c = 0; c < count; c++) {
							const float s = count == 1 ? 0.0f : (c == 0 ? -1.0f : 1.0f);
							const glm::vec3 p = centre + side * (s * half);
							Fox::Graphics::Vulkan::Vertex v{};
							v.position[0] = p.x; v.position[1] = p.y; v.position[2] = p.z; v.position[3] = 1.0f;
							v.normal[0] = normal.x; v.normal[1] = normal.y; v.normal[2] = normal.z;
							v.uv[0] = count == 1 ? 0.5f : static_cast<float>(c);
							v.uv[1] = t;
							v.tangent[0] = side.x; v.tangent[1] = side.y; v.tangent[2] = side.z; v.tangent[3] = 1.0f;
							vertices.push_back(v);
						}
					}
					for (int k = 0; k < SEGMENTS; k++) {
						const uint32_t l0 = first + 2 * k, r0 = l0 + 1;
						if (k < SEGMENTS - 1) {
							const uint32_t l1 = l0 + 2, r1 = l0 + 3;
							indices.insert(indices.end(), { l0, r0, l1, r0, r1, l1 });
						}
						else {
							indices.insert(indices.end(), { l0, r0, l0 + 2 });
						}
					}
				}
			}

			void MainRayTracingScene::PlaceClutter(const std::vector<Entity>& entities, std::string& problems) {
				auto& registry = graph.GetRegistry();

				// Bodies of water: no clutter on the ground under them (their rectangle, below their surface)
				ponds.clear();
				for (Entity entity : entities) {
					const Fox::Scene::WaterComponent* water = registry.TryGet<Fox::Scene::WaterComponent>(entity);
					if (water && water->enabled && graph.IsActiveInHierarchy(entity)) {
						ponds.push_back({ glm::vec3(registry.Get<Fox::Scene::WorldTransformComponent>(entity).matrix[3]), 0.5f * water->size });
					}
				}
				auto underWater = [this](const glm::vec3& p) {
					for (const Pond& pond : ponds) {
						if (std::abs(p.x - pond.centre.x) < pond.half.x && std::abs(p.z - pond.centre.z) < pond.half.y && p.y < pond.centre.y + 0.05f) {
							return true;
						}
					}
					return false;
				};
				for (Entity entity : entities) {
					Fox::Scene::ClutterComponent* clutter = registry.TryGet<Fox::Scene::ClutterComponent>(entity);
					if (!clutter) {
						continue;
					}
					const glm::mat4 world = registry.Get<Fox::Scene::WorldTransformComponent>(entity).matrix;
					clutter->placed = true;
					clutter->placedEnabled = clutter->enabled;
					clutter->placedMatrix = world;
					clutter->movedAgo = -1.0f;
					clutter->object = SIZE_MAX;
					clutter->copies = 0;
					clutter->triangles = 0;
					clutter->problem.clear();
					clutter->positions.clear();
					clutter->shapes.clear();
					clutter->turns.clear();
					clutter->phases.clear();
					if (!clutter->enabled) {
						continue;
					}

					// --- Where: a jittered grid over the area (even, without a grid's look) ---
					const glm::vec3 centre(world[3]);
					glm::vec3 xAxis(world[0].x, 0.0f, world[0].z);
					xAxis = glm::dot(xAxis, xAxis) > 1e-8f ? glm::normalize(xAxis) : glm::vec3(1.0f, 0.0f, 0.0f);
					const glm::vec3 zAxis = glm::cross(xAxis, glm::vec3(0.0f, 1.0f, 0.0f));
					const glm::vec2 size = glm::max(clutter->size, glm::vec2(0.1f));
					const size_t MAX_COPIES = MAX_CLUTTER_INSTANCES;
					float cell = 1.0f / std::sqrt(std::clamp(clutter->density, 0.001f, 1000.0f));
					if (size.x * size.y / (cell * cell) > MAX_COPIES) {
						cell = std::sqrt(size.x * size.y / MAX_COPIES);
						clutter->problem = "thinned out: at most " + std::to_string(MAX_COPIES) + " copies";
					}
					const int nx = std::max(1, static_cast<int>(std::ceil(size.x / cell)));
					const int nz = std::max(1, static_cast<int>(std::ceil(size.y / cell)));

					std::mt19937 rng(clutter->seed);
					std::uniform_real_distribution<float> unit(0.0f, 1.0f);
					const float minSlopeCos = std::cos(glm::radians(std::clamp(clutter->maxSlope, 0.0f, 90.0f)));
					const float e = std::max(0.25f, cell * 0.5f);
					std::vector<glm::mat4> items;
					for (int iz = 0; iz < nz; iz++) {
						for (int ix = 0; ix < nx; ix++) {
							const float lx = -0.5f * size.x + (ix + unit(rng)) * size.x / nx;
							const float lz = -0.5f * size.y + (iz + unit(rng)) * size.y / nz;
							const float scaleRandom = unit(rng), turnRandom = unit(rng), phaseRandom = unit(rng), layerRandom = unit(rng), fadeRandom = unit(rng);
							// Thinning out towards the edges
							if (clutter->edgeFade > 0.0f) {
								const float inside = std::min(0.5f * size.x - std::abs(lx), 0.5f * size.y - std::abs(lz))
									/ (clutter->edgeFade * 0.5f * std::min(size.x, size.y));
								if (glm::smoothstep(0.0f, 1.0f, inside) <= fadeRandom) {
									continue;
								}
							}
							glm::vec3 p = centre + xAxis * lx + zAxis * lz;
							p.y = GetGroundHeightAt(p.x, p.z);
							const glm::vec3 normal = glm::normalize(glm::vec3(
								GetGroundHeightAt(p.x - e, p.z) - GetGroundHeightAt(p.x + e, p.z), 2.0f * e,
								GetGroundHeightAt(p.x, p.z - e) - GetGroundHeightAt(p.x, p.z + e)));
							if (normal.y < minSlopeCos || underWater(p)) {
								continue; // Too steep, or under water
							}
							// On its terrain layer only: all where it is painted fully, thinning out where it fades
							float weight = 1.0f;
							if (clutter->terrainLayer >= 0 && environment.terrain
								&& terrain.LayerWeightAt(p.x, p.z, clutter->terrainLayer, weight) && glm::smoothstep(0.3f, 0.7f, weight) <= layerRandom) {
								continue;
							}
							const float scale = glm::mix(clutter->minScale, clutter->maxScale, scaleRandom);
							const glm::quat tilt = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
								glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), normal), glm::clamp(clutter->alignToGround, 0.0f, 1.0f));
							p.y -= clutter->sink * scale;
							const glm::mat4 shape = glm::mat4_cast(tilt) * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
							const float turn = clutter->randomTurn ? turnRandom * glm::two_pi<float>() : 0.0f;
							clutter->positions.push_back(p);
							clutter->shapes.push_back(shape);
							clutter->turns.push_back(turn);
							clutter->phases.push_back(phaseRandom * glm::two_pi<float>());
							items.push_back(glm::translate(glm::mat4(1.0f), p) * shape * glm::rotate(glm::mat4(1.0f), turn, glm::vec3(0.0f, 1.0f, 0.0f)));
						}
					}
					if (items.empty()) {
						clutter->problem = "no ground flat enough here";
						continue;
					}

					// --- What: the model, or a tuft of grass ---
					const std::string name = graph.GetName(entity);
					try {
						if (clutter->model.empty()) {
							std::vector<Fox::Graphics::Vulkan::Vertex> tuftVertices;
							std::vector<uint32_t> tuftIndices;
							MakeGrassTuft(*clutter, tuftVertices, tuftIndices);
							Fox::Graphics::Vulkan::Material material;
							material.baseColor[0] = clutter->grassColor.r;
							material.baseColor[1] = clutter->grassColor.g;
							material.baseColor[2] = clutter->grassColor.b;
							material.roughness = 0.55f;
							material.specularWeight = 0.5f;
							clutter->object = AddInstancedSurface(tuftVertices, tuftIndices, material, std::move(items), name);
							clutter->triangles = tuftIndices.size() / 3;
						}
						else {
							const auto& mesh = LoadModel(clutter->model, false);
							clutter->object = AddInstancedMesh(mesh, std::move(items), name);
							clutter->triangles = mesh.GetIndices().size() / 3;
						}
					}
					catch (const std::exception& error) {
						clutter->problem = clutter->model + ": " + error.what();
						problems += std::string(problems.empty() ? "" : "; ") + name + ": " + clutter->problem;
						continue;
					}

					clutter->copies = static_cast<uint32_t>(GetObjectItems(clutter->object).size());
					if (clutter->copies < clutter->positions.size()) {
						clutter->problem = "the clutter budget is full: " + std::to_string(clutter->copies) + " of "
							+ std::to_string(clutter->positions.size()) + " copies placed";
						problems += std::string(problems.empty() ? "" : "; ") + name + ": " + clutter->problem;
						clutter->positions.resize(clutter->copies);
						clutter->shapes.resize(clutter->copies);
						clutter->turns.resize(clutter->copies);
						clutter->phases.resize(clutter->copies);
					}
					objectEntities.resize(GetObjectCount());
					objectEntities[clutter->object] = entity;
					if (!graph.IsActiveInHierarchy(entity)) {
						SetObjectVisible(clutter->object, false);
					}
				}
			}

			void MainRayTracingScene::UpdateClutter(float dt) {
				const glm::vec3 camera = GetClusterLodView().cameraPosition;
				const float windAngle = glm::radians(environment.windDirection);
				Fox::Animation::WindSkeleton::Settings wind;
				wind.windDirection = glm::vec3(std::cos(windAngle), 0.0f, std::sin(windAngle));
				const glm::vec3 leanAxis = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), wind.windDirection));
				const float SWAY_RANGE = 80.0f; // Metres: farther copies keep still (the motion would not show)

				graph.GetRegistry().Each<Fox::Scene::ClutterComponent>([&](Entity entity, Fox::Scene::ClutterComponent& clutter) {
					if (!clutter.placed || clutter.enabled != clutter.placedEnabled) {
						structureChanged = true;
						return;
					}
					if (clutter.object == SIZE_MAX || clutter.object >= GetObjectCount()) {
						return;
					}
					const bool show = graph.IsActiveInHierarchy(entity);
					if (show != IsObjectVisible(clutter.object)) {
						SetObjectVisible(clutter.object, show);
					}

					// Moved: the copies follow at once, and are scattered again over the ground there once it stops
					const glm::mat4& world = graph.GetRegistry().Get<Fox::Scene::WorldTransformComponent>(entity).matrix;
					if (world != clutter.placedMatrix) {
						const glm::mat4 delta = world * glm::inverse(clutter.placedMatrix);
						const ObjectTransform moved = Decompose(delta);
						const ObjectTransform& now = GetObjectTransform(clutter.object);
						if (moved.position != now.position || moved.rotation != now.rotation || moved.scale != now.scale) {
							SetObjectTransform(clutter.object, moved);
							clutter.movedAgo = 0.0f;
						}
					}
					if (clutter.movedAgo >= 0.0f) {
						clutter.movedAgo += dt;
						if (clutter.movedAgo > 0.5f) {
							structureChanged = true;
						}
					}

					// Turning to the camera and swaying: each copy's matrix, rebuilt
					if (!show || (!clutter.faceCamera && clutter.sway <= 0.0f)) {
						return;
					}
					std::vector<glm::mat4>& items = EditObjectItems(clutter.object);
					const glm::mat4 toPlaced = glm::inverse(GetObjectTransform(clutter.object).Matrix());
					const glm::vec3 view = glm::vec3(toPlaced * glm::vec4(camera, 1.0f)); // The camera where the copies were placed
					for (size_t i = 0; i < items.size() && i < clutter.positions.size(); i++) {
						const glm::vec3& p = clutter.positions[i];
						const float distanceSquared = glm::dot(p - view, p - view);
						const bool sways = clutter.sway > 0.0f && distanceSquared < SWAY_RANGE * SWAY_RANGE;
						if (!clutter.faceCamera && !sways) {
							continue;
						}
						const float turn = clutter.faceCamera ? std::atan2(view.x - p.x, view.z - p.z) : clutter.turns[i];
						glm::mat4 matrix = clutter.shapes[i] * glm::rotate(glm::mat4(1.0f), turn, glm::vec3(0.0f, 1.0f, 0.0f));
						if (sways) {
							// Leaning with the gusts (light: no spring), and a flutter of its own
							const float lean = clutter.sway * (2.5f * Fox::Animation::WindSkeleton::LeanTarget(wind, p, time)
								+ 0.05f * std::sin(glm::two_pi<float>() * 1.4f * time + clutter.phases[i]));
							matrix = glm::rotate(glm::mat4(1.0f), lean, leanAxis) * matrix;
						}
						items[i] = glm::translate(glm::mat4(1.0f), p) * matrix;
					}
				});
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddParticles(const glm::vec3& position, Fox::Scene::ParticleComponent::Effect effect) {
				using Effect = Fox::Scene::ParticleComponent::Effect;
				const Entity entity = graph.CreateEntity(effect == Effect::Fire ? "Fire" : effect == Effect::Magic ? "Magic"
					: effect == Effect::Orbs ? "Magic orbs" : "Falling leaves");
				Fox::Scene::Transform transform;
				transform.position = position;
				graph.SetWorldTransform(entity, transform);
				graph.GetRegistry().Add<Fox::Scene::ParticleComponent>(entity, Fox::Scene::ParticleComponent::Preset(effect));
				structureChanged = true;
				return entity;
			}

			void MainRayTracingScene::PlaceParticles(const std::vector<Entity>& entities, std::string& problems) {
				using Effect = Fox::Scene::ParticleComponent::Effect;
				auto& registry = graph.GetRegistry();
				Fox::Scene::TextureLibrary& library = *textureLibrary;
				for (Entity entity : entities) {
					Fox::Scene::ParticleComponent* emitter = registry.TryGet<Fox::Scene::ParticleComponent>(entity);
					if (!emitter) {
						continue;
					}
					emitter->placed = true;
					emitter->placedEffect = emitter->effect;
					emitter->placedMax = emitter->maxParticles;
					emitter->placedTexture = emitter->texture;
					emitter->surface = SIZE_MAX;
					emitter->lights.clear();
					emitter->particles.assign(std::clamp<uint32_t>(emitter->maxParticles, 1, 16384), Fox::Scene::Particle{});
					emitter->problem.clear();
					if (!emitter->enabled) {
						continue;
					}

					// --- Its look ---
					Fox::Graphics::Vulkan::Material material;
					material.layerMaterial = -3; // Particles: brightness and heat per vertex (Shaders/common.slang)
					if (emitter->effect == Effect::Leaves) {
						// The albedo, and whatever other maps lie beside it (by their names)
						const std::string albedo = emitter->texture.empty() ? "Textures/vahtera/vahtera_leaf.png" : emitter->texture;
						Fox::Scene::SurfaceTextures maps;
						maps.albedo = albedo;
						std::error_code ignored;
						const std::filesystem::path folder = std::filesystem::path(albedo).parent_path();
						for (const auto& entry : std::filesystem::directory_iterator(folder, ignored)) {
							std::string name = entry.path().filename().string();
							std::string ext = entry.path().extension().string();
							std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
							std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
							if ((ext != ".png" && ext != ".jpg" && ext != ".jpeg") || entry.path().generic_string() == std::filesystem::path(albedo).generic_string()) {
								continue;
							}
							const std::string path = entry.path().generic_string();
							if (name.find("normal") != std::string::npos) maps.normal = path;
							else if (name.find("rough") != std::string::npos) maps.roughness = path;
							else if (name.find("metal") != std::string::npos) maps.metallic = path;
							else if (name.find("occlusion") != std::string::npos || name.find("_ao") != std::string::npos || name.find("ao.") == 0
								|| name.find("ambient") != std::string::npos) maps.occlusion = path;
						}
						material.baseColor[0] = emitter->color.r;
						material.baseColor[1] = emitter->color.g;
						material.baseColor[2] = emitter->color.b;
						material.roughness = 0.8f; // Leaves are matt: a glossy one lying flat would mirror the sky at grazing angles
						material.baseColorTexture = library.Texture(maps.albedo, true);
						if (material.baseColorTexture < 0) {
							emitter->problem = "cannot read " + maps.albedo;
							problems += std::string(problems.empty() ? "" : "; ") + graph.GetName(entity) + ": " + emitter->problem;
							continue;
						}
						material.normalTexture = library.Texture(maps.normal, false);
						const bool packed = !(maps.occlusion.empty() && maps.roughness.empty() && maps.metallic.empty());
						if (packed) {
							const int texture = library.PackedOcclusionRoughnessMetallic(maps.occlusion, maps.roughness, maps.metallic);
							material.metallicRoughnessTexture = texture;
							material.occlusionTexture = maps.occlusion.empty() ? -1 : texture;
							material.roughness = 1.0f;
						}
						material.metallic = packed ? 1.0f : 0.0f;
						material.alphaCutoff = 0.5f;
						material.specularWeight = 0.25f;
					}
					else {
						const bool fire = emitter->effect == Effect::Fire;
						const bool orbs = emitter->effect == Effect::Orbs;
						const std::string sprite = fire ? "Textures/particles/flame.png" : orbs ? "Textures/particles/orb.png" : "Textures/particles/spark.png";
						const glm::vec3 glow = (fire ? glm::vec3(1.0f) : emitter->color) * std::max(emitter->brightness, 0.0f);
						material.baseColor[0] = material.baseColor[1] = material.baseColor[2] = fire ? 0.04f : 0.1f; // Soot, as it cools
						if (orbs) {
							// The second colour (Shaders/common.slang, PARTICLE_MATERIAL)
							material.baseColor[0] = emitter->color2.r;
							material.baseColor[1] = emitter->color2.g;
							material.baseColor[2] = emitter->color2.b;
						}
						material.emissive[0] = glow.r;
						material.emissive[1] = glow.g;
						material.emissive[2] = glow.b;
						material.baseColorTexture = library.Texture(sprite, true);
						material.emissiveTexture = material.baseColorTexture;
						material.alphaCutoff = fire ? 0.3f : orbs ? 0.5f : 0.25f;
						material.roughness = 1.0f;
						material.specularWeight = 0.0f;
					}

					// --- A quad per particle, out of sight (NaN: no triangle) until it is born ---
					const uint32_t count = static_cast<uint32_t>(emitter->particles.size());
					const float hidden = std::numeric_limits<float>::quiet_NaN();
					std::vector<Fox::Graphics::Vulkan::Vertex> quadVertices(count * 4);
					for (Fox::Graphics::Vulkan::Vertex& v : quadVertices) {
						v = Fox::Graphics::Vulkan::Vertex{};
						v.position[0] = v.position[1] = v.position[2] = hidden;
						v.position[3] = 1.0f;
					}
					std::vector<uint32_t> quadIndices;
					quadIndices.reserve(count * 6);
					for (uint32_t i = 0; i < count; i++) {
						const uint32_t b = i * 4;
						quadIndices.insert(quadIndices.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
					}
					emitter->surface = AddSimulatedSurface(quadVertices, quadIndices, material, graph.GetName(entity));
					if (emitter->effect != Effect::Leaves) {
						SetObjectMask(GetSurfaceObject(emitter->surface), INSTANCE_MASK_EFFECT); // Glows; casts no shadow
					}
					objectEntities.resize(GetObjectCount());
					objectEntities[GetSurfaceObject(emitter->surface)] = entity;

					// --- Its lights: one for fire and magic, one per orb of the first dozen ---
					if (emitter->effect != Effect::Leaves && emitter->light) {
						const size_t lightCount = emitter->effect == Effect::Orbs ? std::min<size_t>(12, OrbSlots(*emitter)) : 1;
						for (size_t i = 0; i < lightCount; i++) {
							Fox::Graphics::Vulkan::Light light{};
							light.type = 1; // Point
							light.position = glm::vec3(registry.Get<Fox::Scene::WorldTransformComponent>(entity).matrix[3]);
							light.normal = glm::vec3(0.0f, -1.0f, 0.0f);
							light.area = emitter->effect == Effect::Orbs ? 0.5f * emitter->size : std::max(emitter->spread, 0.05f);
							light.color = glm::vec3(0.0f); // Lit by UpdateParticles
							emitter->lights.push_back(RayTracingScene::AddLight(light));
						}
					}
				}
			}

			void MainRayTracingScene::UpdateParticles(float dt) {
				particleTime += dt;
				graph.GetRegistry().Each<Fox::Scene::ParticleComponent>([&](Entity entity, Fox::Scene::ParticleComponent& emitter) {
					// What needs the rebuild: on or off, another effect, room for more, another texture
					if (!emitter.placed || emitter.effect != emitter.placedEffect || emitter.maxParticles != emitter.placedMax
						|| emitter.texture != emitter.placedTexture
						|| (emitter.enabled && emitter.surface == SIZE_MAX && emitter.problem.empty())
						|| (!emitter.enabled && emitter.surface != SIZE_MAX)) {
						structureChanged = true;
						return;
					}
					if (emitter.surface == SIZE_MAX) {
						return;
					}
					SimulateParticles(entity, emitter, dt);
				});
			}

			void MainRayTracingScene::SimulateParticles(Entity entity, Fox::Scene::ParticleComponent& emitter, float dt) {
				using Effect = Fox::Scene::ParticleComponent::Effect;
				using Fox::Scene::Particle;
				const glm::mat4& world = graph.GetRegistry().Get<Fox::Scene::WorldTransformComponent>(entity).matrix;
				const glm::vec3 origin(world[3]);
				const bool active = graph.IsActiveInHierarchy(entity);
				dt = std::min(dt, 0.1f);

				// A small fast random generator of its own (xorshift)
				auto random = [&emitter]() {
					uint32_t x = emitter.random ? emitter.random : 1u;
					x ^= x << 13; x ^= x >> 17; x ^= x << 5;
					emitter.random = x;
					return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0x1000000);
				};

				// The scene's wind, gusting
				const float windAngle = glm::radians(environment.windDirection);
				Fox::Animation::WindSkeleton::Settings wind;
				wind.windDirection = glm::vec3(std::cos(windAngle), 0.0f, std::sin(windAngle));
				wind.windSpeed = std::max(emitter.windSpeed, 0.0f);
				auto windAt = [&](const glm::vec3& p) {
					return wind.windDirection * Fox::Animation::WindSkeleton::GustSpeed(wind, p, particleTime);
				};
				auto floorAt = [this](const glm::vec3& p) {
					float y = GetGroundHeightAt(p.x, p.z);
					for (const Pond& pond : ponds) {
						if (std::abs(p.x - pond.centre.x) < pond.half.x && std::abs(p.z - pond.centre.z) < pond.half.y) {
							y = std::max(y, pond.centre.y); // Floats on the water
						}
					}
					return y;
				};

				// --- Births ---
				if (active && dt > 0.0f) {
					emitter.spawnCarry += std::max(emitter.rate, 0.0f) * dt;
				}
				size_t next = 0;
				const size_t orbSlots = OrbSlots(emitter);
				size_t nextOrb = 0, nextMote = orbSlots;
				while (emitter.spawnCarry >= 1.0f) {
					emitter.spawnCarry -= 1.0f;
					size_t slot;
					bool mote = false;
					if (emitter.effect == Effect::Orbs) {
						// Orbs in the first slots (the lit ones first), motes after: a mote three times in four
						mote = random() < 0.75f;
						size_t& cursor = mote ? nextMote : nextOrb;
						const size_t end = mote ? emitter.particles.size() : orbSlots;
						while (cursor < end && emitter.particles[cursor].alive) cursor++;
						if (cursor >= end) {
							continue; // That kind is full
						}
						slot = cursor;
					}
					else {
						while (next < emitter.particles.size() && emitter.particles[next].alive) next++;
						if (next == emitter.particles.size()) {
							emitter.spawnCarry = 0.0f; // Full: born when there is room
							break;
						}
						slot = next;
					}
					Particle& p = emitter.particles[slot];
					p = Particle{};
					p.alive = true;
					p.life = std::max(emitter.lifetime, 0.05f) * (0.7f + 0.6f * random());
					p.phase = random() * glm::two_pi<float>();
					p.size = emitter.size * (0.75f + 0.5f * random());
					const float angle = random() * glm::two_pi<float>();
					if (emitter.effect == Effect::Fire) {
						const float r = std::sqrt(random()) * emitter.spread;
						p.position = origin + glm::vec3(std::cos(angle) * r, 0.05f, std::sin(angle) * r);
						p.velocity = glm::vec3((random() - 0.5f) * 0.3f, emitter.speed * (0.7f + 0.6f * random()), (random() - 0.5f) * 0.3f);
						p.spin.x = (random() - 0.5f) * 2.0f;
					}
					else if (emitter.effect == Effect::Magic) {
						p.position = origin + glm::vec3(std::cos(angle) * emitter.spread, 0.1f + random() * 0.3f, std::sin(angle) * emitter.spread);
						p.spin.x = angle; // Its angle around the axis
					}
					else if (emitter.effect == Effect::Orbs) {
						p.mote = mote;
						p.shade = random();
						p.position = origin + glm::vec3((random() * 2.0f - 1.0f) * emitter.spread, 0.0f, (random() * 2.0f - 1.0f) * emitter.spread);
						p.hover = mote ? 0.2f + 2.5f * random() : std::max(emitter.height, 0.1f) * (0.5f + random());
						p.position.y = floorAt(p.position) + p.hover;
						if (mote) {
							p.life *= 0.4f;
							p.size = emitter.size * 0.22f * (0.6f + 0.8f * random());
						}
					}
					else {
						p.position = origin + glm::vec3((random() * 2.0f - 1.0f) * emitter.spread, emitter.height + random() * 1.5f,
							(random() * 2.0f - 1.0f) * emitter.spread);
						const glm::vec3 axis = glm::normalize(glm::vec3(random() - 0.5f, random() - 0.5f, random() - 0.5f) + glm::vec3(1e-4f));
						p.orientation = glm::angleAxis(random() * glm::two_pi<float>(), axis);
						p.velocity = windAt(p.position) * 0.5f + glm::vec3(random() - 0.5f, random() - 0.5f, random() - 0.5f) * emitter.speed;
					}
					p.previousPosition = p.position;
					p.previousOrientation = p.orientation;
					p.previousSize = p.size;
				}

				// --- Motion ---
				float glowSum = 0.0f;
				for (Particle& p : emitter.particles) {
					if (!p.alive) continue;
					p.previousPosition = p.position;
					p.previousOrientation = p.orientation;
					p.previousSize = p.size;
					p.age += dt;
					if (p.age >= p.life) {
						p.alive = false;
						continue;
					}
					const float t = p.age / p.life;

					if (emitter.effect == Effect::Fire) {
						// Buoyant and turbulent, carried off by the wind as it rises; swelling, then shrinking to soot
						const glm::vec3 turbulence(std::sin(7.0f * particleTime + p.phase), 0.0f, std::cos(6.0f * particleTime + 1.7f * p.phase));
						p.velocity += (glm::vec3(0.0f, 2.5f, 0.0f) + turbulence * 1.8f) * dt;
						p.velocity += (windAt(p.position) * 0.35f - glm::vec3(p.velocity.x, 0.0f, p.velocity.z)) * std::min(1.5f * dt, 1.0f);
						p.position += p.velocity * dt;
						p.spin.x += 0.0f;
						glowSum += 1.0f - t;
					}
					else if (emitter.effect == Effect::Magic) {
						// Swirling up around the axis, the ring breathing in and out
						const float radius = emitter.spread * (1.0f - 0.5f * t) * (1.0f + 0.15f * std::sin(3.0f * p.age + p.phase));
						p.spin.x += emitter.speed / std::max(radius, 0.05f) * dt;
						const glm::vec3 drift = windAt(origin) * (0.3f * p.age);
						p.position = origin + drift + glm::vec3(std::cos(p.spin.x) * radius, 0.1f + 1.2f * t * emitter.speed * 0.6f, std::sin(p.spin.x) * radius);
						glowSum += 1.0f - t;
					}
					else if (emitter.effect == Effect::Orbs) {
						// Wandering slowly where its own currents take it, a breath of wind, hovering and bobbing over the ground
						const float s = std::max(emitter.speed, 0.0f) * (p.mote ? 0.6f : 1.0f);
						const glm::vec3 wander(std::sin(0.37f * p.age + p.phase) + 0.5f * std::sin(0.91f * p.age + 2.0f * p.phase), 0.0f,
							std::cos(0.29f * p.age + 1.3f * p.phase) + 0.5f * std::cos(0.83f * p.age + 3.0f * p.phase));
						const glm::vec3 wanted = wander * s + windAt(p.position) * 0.15f;
						p.velocity += (wanted - p.velocity) * std::min(0.8f * dt, 1.0f);
						p.position += p.velocity * dt;
						const float bob = (p.mote ? 0.1f : 0.25f) * std::sin(1.1f * p.age + p.phase);
						const float target = floorAt(p.position) + p.hover + bob;
						p.position.y += (target - p.position.y) * std::min(1.5f * dt, 1.0f);
						glowSum += 1.0f - t;
					}
					else if (!p.landed) {
						// --- A leaf: a rigid plate in the wind ---
						const float RHO = 1.2f;            // Air, kg/m^3
						const float AREA_FILL = 0.6f;      // The leaf covers this much of its quad
						const float AREAL_DENSITY = 0.15f; // kg/m^2 of leaf
						const float NORMAL_COEFFICIENT = 1.2f, FRICTION_COEFFICIENT = 0.05f;
						const float area = AREA_FILL * p.size * p.size;
						const float mass = AREAL_DENSITY * area;
						// A square plate's moments of inertia about its centre (body axes: x and z in the plane, y its normal)
						const glm::vec3 inertia(mass * p.size * p.size / 12.0f, mass * p.size * p.size / 6.0f, mass * p.size * p.size / 12.0f);

						const int substeps = std::clamp(static_cast<int>(std::ceil(dt / (1.0f / 600.0f))), 1, 60);
						const float h = dt / static_cast<float>(substeps);
						const glm::vec3 air = windAt(p.position) + 0.4f * glm::vec3(std::sin(1.3f * particleTime + p.phase), 0.3f * std::sin(2.1f * particleTime + 2.0f * p.phase),
							std::cos(1.1f * particleTime + 3.0f * p.phase)); // Gusts and eddies
						for (int s = 0; s < substeps; s++) {
							const glm::vec3 normal = p.orientation * glm::vec3(0.0f, 1.0f, 0.0f);
							const glm::vec3 relative = air - p.velocity; // The air as the leaf feels it
							const float speed = glm::length(relative);
							glm::vec3 force(0.0f, -9.81f * mass, 0.0f);
							glm::vec3 torque(0.0f);
							if (speed > 1e-4f) {
								const glm::vec3 flow = relative / speed;
								const float facing = glm::dot(normal, flow); // cos of the angle between the normal and the flow
								// The area facing the flow (projected): the pressure on it pushes along the normal
								const float projectedArea = area * std::abs(facing);
								const glm::vec3 pressure = (0.5f * RHO * NORMAL_COEFFICIENT * projectedArea * speed * speed * (facing >= 0.0f ? 1.0f : -1.0f)) * normal;
								const glm::vec3 inPlane = relative - normal * glm::dot(normal, relative);
								const glm::vec3 friction = 0.5f * RHO * FRICTION_COEFFICIENT * area * speed * inPlane;
								force += pressure + friction;
								// The centre of pressure: upwind of the middle by up to a quarter of the leaf, the more the flow
								// runs along it; at the middle when it meets the leaf head-on. The pressure there turns the leaf.
								const float inPlaneLength = glm::length(inPlane);
								if (inPlaneLength > 1e-5f) {
									const glm::vec3 upwind = -inPlane / inPlaneLength;
									const glm::vec3 lever = upwind * (0.25f * p.size * (1.0f - std::abs(facing)));
									torque += glm::cross(lever, pressure);
								}
							}
							// The air resists turning, as it resists moving (in proportion to the area)
							const float spinSpeed = glm::length(p.spin);
							torque -= p.spin * (0.02f * RHO * area * p.size * p.size * (speed + 0.5f * p.size * spinSpeed));

							p.velocity += force / mass * h;
							p.position += p.velocity * h;

							// Rigid body rotation in the leaf's own frame (with the gyroscopic term)
							const glm::quat toBody = glm::conjugate(p.orientation);
							const glm::vec3 spinBody = toBody * p.spin;
							const glm::vec3 torqueBody = toBody * torque;
							const glm::vec3 angular = (torqueBody - glm::cross(spinBody, inertia * spinBody)) / inertia;
							p.spin = p.orientation * (spinBody + angular * h);
							const float maxSpin = 40.0f;
							if (glm::length(p.spin) > maxSpin) p.spin *= maxSpin / glm::length(p.spin);
							p.orientation = glm::normalize(p.orientation + (glm::quat(0.0f, p.spin) * p.orientation) * (0.5f * h));
						}

						// Landing: lies flat (turned as it was about the up axis) on the ground or the water
						const float floor = floorAt(p.position);
						if (p.position.y < floor + 0.01f) {
							p.position.y = floor + 0.01f;
							p.landed = true;
							p.velocity = glm::vec3(0.0f);
							p.spin = glm::vec3(0.0f);
							const glm::vec3 forward = p.orientation * glm::vec3(0.0f, 0.0f, 1.0f);
							const float yaw = std::atan2(forward.x, forward.z);
							p.orientation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
							p.previousOrientation = p.orientation;
						}
					}
				}

				// --- The light: flickering with the flames (or the sparks) ---
				// Orbs: a light each (the first ones), following it, in its shade, fading with it
				auto orbFade = [](const Particle& p) {
					return glm::clamp(std::min(p.age, p.life - p.age) / 1.5f, 0.0f, 1.0f);
				};
				auto orbPulse = [this](const Particle& p) {
					return p.mote ? 0.4f + 0.6f * std::pow(0.5f + 0.5f * std::sin(9.0f * particleTime + 7.0f * p.phase), 3.0f)
						: 0.8f + 0.2f * std::sin(2.3f * p.age + p.phase);
				};
				if (emitter.effect == Effect::Orbs) {
					for (size_t i = 0; i < emitter.lights.size() && i < emitter.particles.size(); i++) {
						const Particle& p = emitter.particles[i];
						Fox::Graphics::Vulkan::Light light{};
						light.type = 1;
						light.position = p.alive ? p.position : origin;
						light.normal = glm::vec3(0.0f, -1.0f, 0.0f);
						light.area = 0.5f * emitter.size;
						const glm::vec3 shade = glm::mix(emitter.color, emitter.color2, p.shade);
						light.color = (active && emitter.light && p.alive) ? shade * (emitter.lightIntensity * orbFade(p) * orbPulse(p)) : glm::vec3(0.0f);
						SetLight(emitter.lights[i], light);
					}
				}
				else if (!emitter.lights.empty()) {
					Fox::Graphics::Vulkan::Light light{};
					light.type = 1;
					light.position = origin + glm::vec3(0.0f, emitter.effect == Effect::Fire ? 0.5f : 0.7f, 0.0f);
					light.normal = glm::vec3(0.0f, -1.0f, 0.0f);
					light.area = std::max(emitter.spread, 0.05f);
					const float flicker = emitter.effect == Effect::Fire
						? 0.75f + 0.15f * std::sin(17.0f * particleTime) + 0.1f * std::sin(41.0f * particleTime + 1.3f)
						: 0.9f + 0.1f * std::sin(9.0f * particleTime);
					const float amount = emitter.particles.empty() ? 0.0f : std::min(glowSum / (0.5f * std::max(emitter.rate, 1.0f) * emitter.lifetime), 1.0f);
					light.color = (active && emitter.light) ? emitter.color * (emitter.lightIntensity * flicker * amount) : glm::vec3(0.0f);
					SetLight(emitter.lights[0], light);
				}

				// --- The quads ---
				const glm::vec3 camera = GetClusterLodView().cameraPosition;
				const float hidden = std::numeric_limits<float>::quiet_NaN();
				const size_t count = emitter.particles.size();
				std::vector<Fox::Graphics::Vulkan::Vertex> quads(count * 4);
				std::vector<glm::vec4> previous(count * 4);
				const glm::vec2 corners[4] = { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
				for (size_t i = 0; i < count; i++) {
					const Particle& p = emitter.particles[i];
					Fox::Graphics::Vulkan::Vertex* v = &quads[i * 4];
					if (!p.alive || !active) {
						for (int c = 0; c < 4; c++) {
							v[c] = Fox::Graphics::Vulkan::Vertex{};
							v[c].position[0] = v[c].position[1] = v[c].position[2] = hidden;
							previous[i * 4 + c] = glm::vec4(hidden, hidden, hidden, 1.0f);
						}
						continue;
					}
					const float t = p.age / p.life;
					glm::vec3 right, up, normal, prevRight, prevUp;
					float size = p.size, previousSize = p.previousSize;
					float brightness = 1.0f, heat = -1.0f;
					if (emitter.effect == Effect::Leaves) {
						right = p.orientation * glm::vec3(1.0f, 0.0f, 0.0f);
						up = p.orientation * glm::vec3(0.0f, 0.0f, -1.0f);
						normal = p.orientation * glm::vec3(0.0f, 1.0f, 0.0f);
						prevRight = p.previousOrientation * glm::vec3(1.0f, 0.0f, 0.0f);
						prevUp = p.previousOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
						const float fade = glm::clamp((p.life - p.age) / 0.6f, 0.0f, 1.0f); // Shrinks away at the end
						size *= fade;
						previousSize *= glm::clamp((p.life - p.age + dt) / 0.6f, 0.0f, 1.0f);
						brightness = 0.0f;
					}
					else {
						// Facing the camera, turned in its plane
						normal = camera - p.position;
						normal = glm::dot(normal, normal) > 1e-8f ? glm::normalize(normal) : glm::vec3(0.0f, 0.0f, 1.0f);
						glm::vec3 side = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), normal);
						side = glm::dot(side, side) > 1e-8f ? glm::normalize(side) : glm::vec3(1.0f, 0.0f, 0.0f);
						const glm::vec3 top = glm::cross(normal, side);
						const float turn = emitter.effect == Effect::Fire ? 0.3f * std::sin(p.phase + 2.0f * p.age) : p.phase + 3.0f * p.age;
						right = side * std::cos(turn) + top * std::sin(turn);
						up = top * std::cos(turn) - side * std::sin(turn);
						prevRight = right;
						prevUp = up;
						if (emitter.effect == Effect::Fire) {
							// Swells as it rises, then shrinks; cools from white-yellow through orange to red, then soot
							const float swell = 0.6f + 0.9f * std::sin(glm::pi<float>() * std::min(t * 1.1f, 1.0f));
							size *= swell;
							previousSize *= swell;
							heat = glm::clamp(1.0f - 1.15f * t, 0.0f, 1.0f);
							brightness = glm::clamp(1.0f - t, 0.0f, 1.0f) * (0.85f + 0.15f * std::sin(23.0f * particleTime + p.phase));
						}
						else if (emitter.effect == Effect::Orbs) {
							// Fading in and out, pulsing (motes twinkle); its shade between the two colours
							const float fade = orbFade(p);
							brightness = fade * orbPulse(p);
							size *= std::sqrt(fade);
							previousSize = size;
							heat = -1.0f - p.shade;
						}
						else {
							const float twinkle = 0.7f + 0.3f * std::sin(13.0f * particleTime + 5.0f * p.phase);
							brightness = glm::clamp(std::min(t * 6.0f, (1.0f - t) * 2.0f), 0.0f, 1.0f) * twinkle;
							size *= 0.5f + 0.5f * (1.0f - t);
							previousSize = size;
						}
					}
					for (int c = 0; c < 4; c++) {
						const glm::vec3 at = p.position + (right * corners[c].x + up * corners[c].y) * size;
						const glm::vec3 was = p.previousPosition + (prevRight * corners[c].x + prevUp * corners[c].y) * previousSize;
						Fox::Graphics::Vulkan::Vertex& vertex = v[c];
						vertex = Fox::Graphics::Vulkan::Vertex{};
						vertex.position[0] = at.x; vertex.position[1] = at.y; vertex.position[2] = at.z; vertex.position[3] = 1.0f;
						vertex.normal[0] = normal.x; vertex.normal[1] = normal.y; vertex.normal[2] = normal.z;
						vertex.uv[0] = corners[c].x + 0.5f;
						vertex.uv[1] = 0.5f - corners[c].y; // Top of the image up
						vertex.tangent[0] = right.x; vertex.tangent[1] = right.y; vertex.tangent[2] = right.z; vertex.tangent[3] = 1.0f;
						vertex.pad[0] = brightness;
						vertex.pad[1] = heat;
						previous[i * 4 + c] = glm::vec4(was, 1.0f);
					}
				}
				UpdateSurfaceVertices(emitter.surface, 0, quads.data(), static_cast<uint32_t>(quads.size()));
				UpdateSurfacePrevious(emitter.surface, previous.data(), static_cast<uint32_t>(previous.size()));
				MarkSurfaceDeformed(emitter.surface, true); // They fly far: built anew every frame
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddText(const glm::vec3& position, Fox::Scene::TextComponent::Space space,
				Fox::Scene::TextComponent::Style style, bool billboard, const std::string& font) {
				using Fox::Scene::TextComponent;
				const bool screen = space == TextComponent::Space::Screen;
				const Entity entity = graph.CreateEntity(screen ? (style == TextComponent::Style::Flat ? "UI text" : "UI 3D text")
					: style == TextComponent::Style::Extruded ? "3D text" : billboard ? "Billboard text" : "Text");
				Fox::Scene::Transform transform;
				transform.position = position + glm::vec3(0.0f, 1.0f, 0.0f);
				graph.SetWorldTransform(entity, transform);
				TextComponent text;
				text.space = space;
				text.style = style;
				text.billboard = billboard;
				text.font = font;
				text.text = screen ? "Score: 0" : "Hello";
				text.size = screen ? (style == TextComponent::Style::Flat ? 64.0f : 90.0f) : 1.0f;
				if (style == TextComponent::Style::Extruded) {
					text.color = glm::vec4(0.95f, 0.75f, 0.3f, 1.0f); // Gold
					text.metallic = 1.0f;
					text.roughness = 0.3f;
				}
				graph.GetRegistry().Add<TextComponent>(entity, text);
				structureChanged = true;
				return entity;
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddSprite() {
				const Entity entity = graph.CreateEntity("UI sprite");
				graph.GetRegistry().Add<Fox::Scene::SpriteComponent>(entity);
				return entity;
			}

			void MainRayTracingScene::SetViewCamera(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up, float fovY, float aspect) {
				viewCamera.position = position;
				viewCamera.forward = glm::normalize(forward);
				viewCamera.up = glm::normalize(up);
				viewCamera.fovY = fovY;
				viewCamera.aspect = aspect;
			}

			std::string MainRayTracingScene::TextKey(const Fox::Scene::TextComponent& text) {
				using Fox::Scene::TextComponent;
				if (!text.enabled) return "off";
				if (text.space == TextComponent::Space::Screen && text.style == TextComponent::Style::Flat) return "ui"; // Drawn, not geometry
				std::ostringstream key;
				key << text.text << '|' << text.font << '|' << static_cast<int>(text.space) << static_cast<int>(text.style) << '|' << text.size << '|'
					<< text.align << '|' << text.lineSpacing << '|' << text.letterSpacing << '|' << text.color.r << ',' << text.color.g << ',' << text.color.b
					<< '|' << text.fill << '|' << text.mask << '|' << text.roughness << '|' << text.metallic << '|' << text.emission << '|' << text.depth
					<< '|' << text.subdivisions << '|' << text.displacement << '|' << text.displacementScale << '|' << text.pivot.x << ',' << text.pivot.y;
				return key.str();
			}

			void MainRayTracingScene::PlaceTexts(const std::vector<Entity>& entities, std::string& problems) {
				using Fox::Scene::TextComponent;
				auto& registry = graph.GetRegistry();
				for (Entity entity : entities) {
					TextComponent* text = registry.TryGet<TextComponent>(entity);
					if (!text) continue;
					text->placed = true;
					text->placedKey = TextKey(*text);
					text->object = SIZE_MAX;
					text->triangles = 0;
					text->problem.clear();
					const bool screen = text->space == TextComponent::Space::Screen;
					if (!text->enabled || (screen && text->style == TextComponent::Style::Flat)) {
						continue; // Off, or drawn by the UI
					}
					const std::string fontFile = text->font.empty() ? Fox::Text::DefaultFont() : text->font;
					auto font = Fox::Text::Font::Get(fontFile);
					if (!font) {
						text->problem = fontFile.empty() ? "no font in Fonts/" : "cannot read " + fontFile;
						problems += std::string(problems.empty() ? "" : "; ") + graph.GetName(entity) + ": " + text->problem;
						continue;
					}
					try {
						// The outlines fine enough for a close look (a thousandth of an em), filled, as a mesh
						const Fox::Text::Layout layout = Fox::Text::LayOut(*font, text->text, static_cast<Fox::Text::Align>(std::clamp(text->align, 0, 2)),
							text->lineSpacing, text->letterSpacing, 0.001f);
						const Fox::Text::Fill fill = Fox::Text::Triangulate(layout.contours);
						if (fill.triangles.empty()) continue;
						Fox::Text::MeshOptions options;
						const float size = screen ? 1.0f : std::max(text->size, 0.001f); // On the screen scaled with the view
						options.size = size;
						options.pivot = text->pivot;
						const bool extruded = text->style == TextComponent::Style::Extruded;
						options.depth = extruded ? std::max(text->depth, 0.0f) * size : 0.0f;
						std::shared_ptr<const Fox::Scene::GrayImage> height;
						if (extruded) {
							options.subdivisions = text->subdivisions;
							if (!text->displacement.empty()) {
								height = textureLibrary->Height(text->displacement, 1, false, "", 0.5f);
								options.displacement = height.get();
								options.displacementScale = text->displacementScale * size;
							}
						}
						std::vector<Fox::Graphics::Vulkan::Vertex> vertices;
						std::vector<uint32_t> indices;
						Fox::Text::BuildMesh(layout, fill, options, vertices, indices);

						Fox::Graphics::Vulkan::Material material;
						material.baseColor[0] = text->color.r;
						material.baseColor[1] = text->color.g;
						material.baseColor[2] = text->color.b;
						material.roughness = text->roughness;
						material.metallic = text->metallic;
						for (int c = 0; c < 3; c++) material.emissive[c] = text->color[c] * std::max(text->emission, 0.0f);
						if (!text->fill.empty() || !text->mask.empty()) {
							material.baseColorTexture = textureLibrary->MaskedTexture(text->fill, text->mask, true);
							material.emissiveTexture = text->emission > 0.0f ? material.baseColorTexture : -1;
							if (!text->mask.empty()) material.alphaCutoff = 0.5f;
						}
						if (screen) {
							// Its glow lights nothing in the world (it is not there): a particle material, full brightness
							material.layerMaterial = -3;
							for (auto& v : vertices) { v.pad[0] = 1.0f; v.pad[1] = -1.0f; }
						}
						const glm::mat4 world = registry.Get<Fox::Scene::WorldTransformComponent>(entity).matrix;
						text->object = AddGeneratedObject(vertices, indices, material, screen ? glm::mat4(1.0f) : world, graph.GetName(entity));
						text->triangles = indices.size() / 3;
						if (screen) {
							SetObjectMask(text->object, INSTANCE_MASK_UI);
						}
						objectEntities.resize(GetObjectCount());
						objectEntities[text->object] = entity;
						if (!graph.IsActiveInHierarchy(entity)) {
							SetObjectVisible(text->object, false);
						}
					}
					catch (const std::exception& error) {
						text->problem = error.what();
						problems += std::string(problems.empty() ? "" : "; ") + graph.GetName(entity) + ": " + text->problem;
					}
				}
			}

			void MainRayTracingScene::UpdateTexts() {
				using Fox::Scene::TextComponent;
				graph.GetRegistry().Each<TextComponent>([&](Entity entity, TextComponent& text) {
					if (!text.placed || TextKey(text) != text.placedKey) {
						structureChanged = true; // Its geometry or look changed: made again with the rebuild
						return;
					}
					if (text.object == SIZE_MAX || text.object >= GetObjectCount()) return;
					const bool show = graph.IsActiveInHierarchy(entity);
					if (show != IsObjectVisible(text.object)) SetObjectVisible(text.object, show);
					// In the world and not turning: where its entity is (the others follow the camera: UpdateViewObjects)
					if (text.space == TextComponent::Space::World && !text.billboard) {
						SetObjectTransform(text.object, graph.GetWorldTransform(entity));
					}
				});
			}

			void MainRayTracingScene::UpdateViewObjects() {
				using Fox::Scene::TextComponent;
				const ViewCamera& view = viewCamera;
				const glm::vec3 right = glm::normalize(glm::cross(view.forward, view.up));
				const glm::vec3 up = glm::normalize(glm::cross(right, view.forward));
				graph.GetRegistry().Each<TextComponent>([&](Entity entity, TextComponent& text) {
					if (text.object == SIZE_MAX || text.object >= GetObjectCount()) return;
					if (text.space == TextComponent::Space::Screen) {
						// Held in front of the camera where its screen position is, facing it, as big as its size in pixels
						const float height = 2.0f * SCREEN_TEXT_DISTANCE * std::tan(0.5f * view.fovY);
						const float width = height * view.aspect;
						ObjectTransform transform;
						transform.position = view.position + view.forward * SCREEN_TEXT_DISTANCE
							+ right * ((text.screenPosition.x - 0.5f) * width) + up * ((0.5f - text.screenPosition.y) * height);
						transform.rotation = glm::quat_cast(glm::mat3(right, up, -view.forward));
						transform.scale = glm::vec3(std::max(text.size, 0.01f) * height / 1080.0f);
						SetObjectTransform(text.object, transform);
					}
					else if (text.billboard) {
						// Turning to face the camera, upright
						ObjectTransform transform = graph.GetWorldTransform(entity);
						glm::vec3 toCamera = view.position - transform.position;
						if (glm::dot(toCamera, toCamera) < 1e-8f) return;
						toCamera = glm::normalize(toCamera);
						const glm::vec3 worldUp = std::abs(toCamera.y) > 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
						transform.rotation = glm::quatLookAt(-toCamera, worldUp);
						SetObjectTransform(text.object, transform);
					}
				});
			}

			float MainRayTracingScene::GetGroundHeight() const {
				return environment.groundHeight;
			}

			float MainRayTracingScene::GetGroundHeightAt(float x, float z) const {
				float y = environment.groundHeight;
				if (environment.terrain && terrain.HeightAt(x, z, y)) {
					return y;
				}
				return environment.groundHeight;
			}

			// --- Terrain ---

			void MainRayTracingScene::ResetTerrain() {
				// The blend map texture stays: a terrain made later reuses it
				const int texture = terrain.blendTexture, resolution = terrain.blendTextureResolution;
				terrain = Fox::Scene::Terrain{};
				terrain.blendTexture = texture;
				terrain.blendTextureResolution = resolution;
			}

			Fox::Graphics::Vulkan::Material MainRayTracingScene::TerrainSurfaceMaterial() const {
				// Read by Shaders/terrain.slang: the blend map as base colour texture, the blend softness in
				// occlusionStrength; layerMaterial is set by AddEditableSurface
				Fox::Graphics::Vulkan::Material material;
				material.baseColorTexture = terrain.blendTexture;
				material.roughness = 1.0f;
				material.metallic = 0.0f;
				material.occlusionStrength = std::max(terrain.settings.blendSoftness, 0.001f);
				return material;
			}

			std::vector<Fox::Graphics::Vulkan::Material> MainRayTracingScene::TerrainLayerMaterials() {
				Fox::Scene::TextureLibrary& library = *textureLibrary;
				std::vector<Fox::Graphics::Vulkan::Material> layers;
				for (const Fox::Scene::TerrainLayer& layer : terrain.settings.layers) {
					// How Shaders/terrain.slang reads a layer: albedo, normal map, packed occlusion / roughness /
					// metallic as for any material; the height map in emissiveTexture; repeats per meter in uvScale;
					// uvRotation 1 for a DirectX normal map
					Fox::Graphics::Vulkan::Material material;
					material.baseColor[0] = layer.tint.r;
					material.baseColor[1] = layer.tint.g;
					material.baseColor[2] = layer.tint.b;
					material.baseColorTexture = library.Texture(layer.albedo, true);
					material.normalTexture = library.Texture(layer.normal, false);
					material.normalScale = layer.normalStrength;
					const bool packed = !(layer.occlusion.empty() && layer.roughness.empty() && layer.metallic.empty());
					if (packed) {
						const int texture = library.PackedOcclusionRoughnessMetallic(layer.occlusion, layer.roughness, layer.metallic);
						material.metallicRoughnessTexture = texture;
						material.occlusionTexture = layer.occlusion.empty() ? -1 : texture;
					}
					material.metallic = packed ? 1.0f : 0.0f;
					material.roughness = layer.roughnessScale;
					material.emissiveTexture = library.Texture(layer.height, false);
					const float repeats = 1.0f / std::max(layer.tiling, 0.01f);
					material.uvScale[0] = repeats;
					material.uvScale[1] = repeats;
					material.uvRotation = layer.normalGreenUp ? 0.0f : 1.0f;
					layers.push_back(material);
				}
				return layers;
			}

			void MainRayTracingScene::PlaceTerrain() {
				if (!terrain.IsCreated()) {
					// Flat at the height of the ground plane it replaces, all grass
					Fox::Scene::TerrainSettings settings = Fox::Scene::TerrainSettings::Defaults();
					settings.position.y = environment.groundHeight;
					terrain.Create(settings);
				}

				// The blend map texture: made once per size, then only its changed texels are uploaded
				const int blendSize = terrain.settings.blendResolution;
				if (terrain.blendTexture < 0 || terrain.blendTextureResolution != blendSize) {
					terrain.blendTexture = textureLibrary->CreateTexture(terrain.BlendPixels(0, 0, blendSize, blendSize), blendSize, blendSize);
					terrain.blendTextureResolution = blendSize;
					int x0, y0, x1, y1;
					terrain.TakeDirtyBlend(x0, y0, x1, y1); // Uploaded with it
				}

				std::vector<Fox::Graphics::Vulkan::Vertex> terrainVertices;
				std::vector<uint32_t> terrainIndices;
				terrain.BuildMesh(terrainVertices, terrainIndices);
				int first, last;
				terrain.TakeDirtyRows(first, last); // The mesh is current

				terrainSurface = AddEditableSurface(terrainVertices, terrainIndices, TerrainSurfaceMaterial(), TerrainLayerMaterials(),
					glm::translate(glm::mat4(1.0f), terrain.settings.position), "Terrain");
				terrainPlaced = true;
			}

			void MainRayTracingScene::ApplyTerrainLayers() {
				if (!terrainPlaced) {
					return;
				}
				const uint32_t index = GetSurfaceMaterial(terrainSurface);
				Fox::Graphics::Vulkan::Material surface = TerrainSurfaceMaterial();
				surface.layerMaterial = static_cast<int>(index + 1);
				SetMaterial(index, surface);
				const std::vector<Fox::Graphics::Vulkan::Material> layers = TerrainLayerMaterials();
				for (size_t i = 0; i < layers.size(); i++) {
					SetMaterial(index + 1 + static_cast<uint32_t>(i), layers[i]);
				}
			}

			void MainRayTracingScene::ApplyTerrainEdits(bool strokeEnded) {
				if (!terrainPlaced) {
					return;
				}
				if (strokeEnded) {
					InvalidatePaths(); // The ground changed under them
				}
				int first, last;
				if (terrain.TakeDirtyRows(first, last)) {
					std::vector<Fox::Graphics::Vulkan::Vertex> rows;
					terrain.BuildRows(first, last, rows);
					const uint32_t n = static_cast<uint32_t>(terrain.settings.resolution);
					UpdateSurfaceVertices(terrainSurface, static_cast<uint32_t>(first) * n, rows.data(), static_cast<uint32_t>(rows.size()));
				}
				int x0, y0, x1, y1;
				if (terrain.TakeDirtyBlend(x0, y0, x1, y1)) {
					UpdateTextureRegion(terrain.blendTexture, x0, y0, x1 - x0, y1 - y0, terrain.BlendPixels(x0, y0, x1, y1));
				}
				if (strokeEnded) {
					RebuildSurface(terrainSurface);
				}
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddTree(const glm::vec3& position, const Tree* copyOf, Entity parent) {
				Fox::Scene::Transform transform;
				if (copyOf) {
					// Turned and sized as the tree it copies
					for (Entity other : GetTreeEntities()) {
						if (GetTree(other) == copyOf) {
							transform = graph.GetWorldTransform(other);
						}
					}
				}
				transform.position = position;

				const Entity entity = graph.CreateEntity("Tree " + std::to_string(GetTreeEntities().size() + 1), parent);
				graph.SetWorldTransform(entity, transform);

				MeshComponent mesh;
				mesh.type = MeshComponent::Type::Tree;
				mesh.tree = copyOf ? copyOf->CloneSettings() : std::make_shared<Tree>();
				MeshChanged(&graph.GetRegistry().Add<MeshComponent>(entity, std::move(mesh)), nullptr);
				return entity;
			}

			std::vector<Fox::Graphics::Vulkan::Material> MainRayTracingScene::MaterialsFor(const TreeLook& look) {
				Fox::Scene::TextureLibrary& library = *textureLibrary;

				// Colour (sRGB) and normal map as they are; occlusion, roughness and metallic packed into one
				// texture the glTF way (R occlusion, G roughness, B metallic)
				auto surface = [&](const SurfaceTextures& textures, const glm::vec3& tint, float roughness) {
					Fox::Graphics::Vulkan::Material material;
					material.baseColor[0] = tint.r;
					material.baseColor[1] = tint.g;
					material.baseColor[2] = tint.b;
					material.roughness = roughness;
					material.baseColorTexture = library.Texture(textures.albedo, true);
					material.normalTexture = library.Texture(textures.normal, false);

					const bool packed = !(textures.occlusion.empty() && textures.roughness.empty() && textures.metallic.empty());
					if (packed) {
						const int texture = library.PackedOcclusionRoughnessMetallic(textures.occlusion, textures.roughness, textures.metallic);
						material.metallicRoughnessTexture = texture;
						material.occlusionTexture = textures.occlusion.empty() ? -1 : texture;
					}
					material.metallic = packed ? 1.0f : 0.0f; // The factor multiplies the texture
					return material;
				};

				Fox::Graphics::Vulkan::Material bark = surface(look.bark, look.barkTint, look.barkRoughness);
				Fox::Graphics::Vulkan::Material leaf = surface(look.leaf, look.leafTint, look.leafRoughness);
				leaf.alphaCutoff = look.alphaCutoff;

				// The leaf alpha builds the opacity micromaps
				if (leaf.baseColorTexture >= 0 && !HasAlphaMask(leaf.baseColorTexture)) {
					if (const auto* alpha = library.Alpha(look.leaf.albedo)) {
						SetAlphaMask(leaf.baseColorTexture, *alpha);
					}
				}

				return { bark, leaf };
			}

			void MainRayTracingScene::GenerateTreeMesh(Tree& tree) {
				const auto start = std::chrono::steady_clock::now();
				Fox::Scene::TextureLibrary& library = *textureLibrary;

				Fox::Graphics::Geometry::TreeSettings settings = tree.shape;
				settings.normalMapGreenUp = tree.look.bark.normalGreenUp;
				settings.leafNormalMapGreenUp = tree.look.leaf.normalGreenUp;

				// Height maps: averaged down to about the triangle size (finer detail would only alias between the
				// vertices; the normal maps carry it). Leaves are flat outside their alpha.
				settings.height = nullptr;
				if (auto bark = library.Height(tree.look.bark.displacement, 16, tree.look.bark.invertDisplacement, "", settings.displacementMidlevel)) {
					settings.height = [bark](float u, float v) { return bark->Sample(u, v); };
				}
				settings.leafHeight = nullptr;
				if (auto leaf = library.Height(tree.look.leaf.displacement, 32, tree.look.leaf.invertDisplacement, tree.look.leaf.albedo, settings.leafDisplacementMidlevel)) {
					settings.leafHeight = [leaf](float u, float v) { return leaf->Sample(u, v); };
				}

				// Cards keep the leaf texture's proportions
				if (const auto* alpha = library.Alpha(tree.look.leaf.albedo); alpha && alpha->height > 0) {
					settings.leafAspect = static_cast<float>(alpha->width) / static_cast<float>(alpha->height);
				}

				tree.mesh = std::make_shared<Fox::Graphics::Geometry::SkinnedMesh>(Fox::Graphics::Geometry::GenerateTree(settings));

				// Cluster level of detail: part 0 is the bark, part 1 the leaf cards
				tree.lod = nullptr;
				if (ClusterLodEnabled()) {
					tree.lod = std::make_shared<Fox::Graphics::Geometry::ClusterLod>(Fox::Graphics::Geometry::BuildClusterLod(*tree.mesh, { false, true }));
					const auto& lod = *tree.lod;
					std::cout << "Cluster LOD: " << lod.clusters.size() << " clusters in " << lod.levels.size() << " levels, "
						<< lod.TriangleCount() << " triangles, " << lod.addedVertices << " vertices added (leaf cards), " << lod.buildSeconds * 1000.0f << " ms\n";
					for (size_t l = 0; l < lod.levels.size(); l++) {
						uint32_t triangles = 0;
						for (uint32_t c = 0; c < lod.levels[l].clusterCount; c++) {
							triangles += lod.clusters[lod.levels[l].firstCluster + c].triangleCount;
						}
						std::cout << "  level " << l << ": " << lod.levels[l].clusterCount << " clusters, " << triangles << " triangles, error "
							<< lod.levels[l].minSelfError << "\n";
					}
				}
				tree.skeleton = std::make_unique<Fox::Animation::WindSkeleton>(tree.mesh->bones, tree.wind);
				tree.size = 0.0f; // Measured again
				tree.regenerate = false;
				tree.generationSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
			}

			bool MainRayTracingScene::ApplyTreeChanges(std::string& error) {
				treesChanged = false;
				graph.UpdateTransforms();
				auto& registry = graph.GetRegistry();
				try {
					// Entities sharing a tree are instances of it: its geometry is generated, stored and deformed once
					const std::vector<Entity> entities = GetTreeEntities();
					std::vector<Tree*> trees;
					std::vector<std::vector<Entity>> users;
					std::unordered_map<Tree*, size_t> indexOf;
					for (Entity entity : entities) {
						Tree* tree = GetTree(entity);
						auto [it, added] = indexOf.emplace(tree, trees.size());
						if (added) {
							trees.push_back(tree);
							users.emplace_back();
						}
						users[it->second].push_back(entity);
					}

					std::vector<DynamicMesh> meshes;
					for (size_t t = 0; t < trees.size(); t++) {
						Tree& tree = *trees[t];
						if (tree.regenerate || !tree.mesh) {
							GenerateTreeMesh(tree);
						}

						DynamicMesh mesh;
						mesh.mesh = tree.mesh.get();
						mesh.lod = tree.lod.get();
						mesh.materials = MaterialsFor(tree.look);
						mesh.transform = graph.GetWorldTransform(users[t][0]);
						mesh.name = graph.GetName(users[t][0]);
						for (size_t u = 1; u < users[t].size(); u++) {
							mesh.moreTransforms.push_back(graph.GetWorldTransform(users[t][u]));
							mesh.moreNames.push_back(graph.GetName(users[t][u]));
						}
						meshes.push_back(std::move(mesh));
					}

					const std::vector<DynamicMeshHandle> handles = RebuildDynamicMeshes(meshes);

					// The previous trees' objects are gone (or someone else's now)
					for (Entity& entity : objectEntities) {
						const MeshComponent* mesh = registry.TryGet<MeshComponent>(entity);
						if (!mesh || mesh->type == MeshComponent::Type::Tree) {
							entity = Entity{};
						}
					}
					objectEntities.resize(GetObjectCount());

					for (size_t t = 0; t < trees.size(); t++) {
						trees[t]->handle = handles[t];
						trees[t]->placed = true;
						for (size_t u = 0; u < users[t].size(); u++) {
							const Entity entity = users[t][u];
							MeshComponent& mesh = registry.Get<MeshComponent>(entity);
							mesh.placed = true;
							mesh.object = u == 0 ? handles[t].object : handles[t].moreObjects[u - 1];
							mesh.placedMatrix = registry.Get<Fox::Scene::WorldTransformComponent>(entity).matrix;
							objectEntities[mesh.object] = entity;
						}
					}
					ForgetStalePlacements();
					SyncMeshes();
					objectsChanged = true;
					return true;
				}
				catch (const std::exception& e) {
					error = e.what();
					return false;
				}
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddTreeInstance(const std::shared_ptr<Tree>& tree, const Fox::Scene::Transform& transform,
				const std::string& name, Entity parent, bool rebuild) {
				const Entity entity = graph.CreateEntity(name, parent);
				graph.SetWorldTransform(entity, transform);
				MeshComponent mesh;
				mesh.type = MeshComponent::Type::Tree;
				mesh.tree = tree;
				MeshComponent& added = graph.GetRegistry().Add<MeshComponent>(entity, std::move(mesh));
				if (rebuild) {
					MeshChanged(&added, nullptr);
				}
				return entity;
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddModelInstance(const std::string& file, bool displacement, const Fox::Scene::Transform& transform,
				const std::string& name, Entity parent, bool rebuild) {
				const Entity entity = graph.CreateEntity(name, parent);
				graph.SetWorldTransform(entity, transform);
				MeshComponent mesh;
				mesh.type = MeshComponent::Type::Model;
				mesh.file = file;
				mesh.displacement = displacement;
				MeshComponent& added = graph.GetRegistry().Add<MeshComponent>(entity, std::move(mesh));
				if (rebuild) {
					MeshChanged(&added, nullptr);
				}
				return entity;
			}

			float MainRayTracingScene::GetModelBottom(const std::string& file, bool displacement) {
				const std::string key = file + (displacement ? "|displaced" : "");
				if (auto it = modelBottoms.find(key); it != modelBottoms.end()) {
					return it->second;
				}
				float bottom = 0.0f;
				try {
					const auto& vertices = LoadModel(file, displacement).GetVertices();
					if (!vertices.empty()) {
						bottom = std::numeric_limits<float>::max();
						for (const auto& vertex : vertices) {
							bottom = std::min(bottom, vertex.position[1]);
						}
					}
				}
				catch (const std::exception& e) {
					std::cout << "Model " << file << ": " << e.what() << "\n";
				}
				modelBottoms[key] = bottom;
				return bottom;
			}

			// --- Water ---

			void MainRayTracingScene::PlaceWater(Entity entity, Fox::Scene::WaterComponent& water) {
				const glm::mat4 world = graph.GetRegistry().Get<Fox::Scene::WorldTransformComponent>(entity).matrix;
				const glm::vec3 center(world[3]);
				water.placedMatrix = world;

				Fox::Scene::WaterSimulation::Grid grid;
				grid.cellSize = std::max(water.cellSize, 0.02f);
				grid.width = std::clamp(static_cast<uint32_t>(std::round(water.size.x / grid.cellSize)), 4u, 512u);
				grid.depth = std::clamp(static_cast<uint32_t>(std::round(water.size.y / grid.cellSize)), 4u, 512u);
				grid.cellSize = std::max(water.size.x / grid.width, water.size.y / grid.depth); // Whole cells over the size
				grid.origin = glm::vec3(center.x - 0.5f * grid.width * grid.cellSize, center.y, center.z - 0.5f * grid.depth * grid.cellSize);

				// The bed under every cell, and the water up to the entity's height
				PendingWater pending;
				pending.entity = entity;
				pending.grid = grid;
				const uint32_t cells = grid.width * grid.depth;
				pending.bed.resize(cells);
				pending.depth.resize(cells);
				std::vector<Fox::Graphics::Vulkan::Vertex> surfaceVertices(cells);
				for (uint32_t j = 0; j < grid.depth; j++) {
					for (uint32_t i = 0; i < grid.width; i++) {
						const uint32_t c = j * grid.width + i;
						const float x = grid.origin.x + (i + 0.5f) * grid.cellSize, z = grid.origin.z + (j + 0.5f) * grid.cellSize;
						const float bed = GetGroundHeightAt(x, z);
						pending.bed[c] = bed;
						pending.depth[c] = std::max(center.y - bed, 0.0f);
						Fox::Graphics::Vulkan::Vertex& vertex = surfaceVertices[c];
						vertex = {};
						vertex.position[0] = x;
						vertex.position[1] = pending.depth[c] > 0.0f ? center.y : bed - 0.05f;
						vertex.position[2] = z;
						vertex.position[3] = 1.0f;
						vertex.normal[1] = 1.0f;
						vertex.uv[0] = (i + 0.5f) / grid.width;
						vertex.uv[1] = (j + 0.5f) / grid.depth;
					}
				}
				std::vector<uint32_t> surfaceIndices;
				surfaceIndices.reserve(static_cast<size_t>(grid.width - 1) * (grid.depth - 1) * 6);
				for (uint32_t j = 0; j + 1 < grid.depth; j++) {
					for (uint32_t i = 0; i + 1 < grid.width; i++) {
						const uint32_t a = j * grid.width + i, b = a + 1, c = a + grid.width, d = c + 1;
						surfaceIndices.insert(surfaceIndices.end(), { a, c, b, b, c, d }); // Facing up
					}
				}

				water.surface = AddSimulatedSurface(surfaceVertices, surfaceIndices, water.ToMaterial(), graph.GetName(entity));

				// Spray: a little octahedron per droplet, inactive (NaN corners) until thrown
				water.hasSpray = water.sprayDroplets > 0;
				if (water.hasSpray) {
					const uint32_t droplets = std::min(water.sprayDroplets, 65536u);
					std::vector<Fox::Graphics::Vulkan::Vertex> dropletVertices(static_cast<size_t>(droplets) * 6);
					for (auto& vertex : dropletVertices) {
						vertex = {};
						vertex.position[0] = std::numeric_limits<float>::quiet_NaN(); // Inactive until thrown
						vertex.position[1] = center.y;
						vertex.position[2] = center.z;
						vertex.position[3] = 1.0f;
						vertex.normal[1] = 1.0f;
					}
					// Corners +x, -x, +y, -y, +z, -z
					const uint32_t faces[8][3] = { { 2, 4, 0 }, { 2, 0, 5 }, { 2, 5, 1 }, { 2, 1, 4 }, { 3, 0, 4 }, { 3, 5, 0 }, { 3, 1, 5 }, { 3, 4, 1 } };
					std::vector<uint32_t> dropletIndices;
					dropletIndices.reserve(static_cast<size_t>(droplets) * 24);
					for (uint32_t d = 0; d < droplets; d++) {
						for (const auto& face : faces) {
							for (uint32_t corner : face) {
								dropletIndices.push_back(d * 6 + corner);
							}
						}
					}
					// A droplet is clear water, but spray is many of them, smaller than a pixel: light bounces between them
					// and scatters every way, so it looks white. (Clear glass this small also traps a path tracer's rays
					// by total internal reflection until they run out of bounces, and turns black.)
					Fox::Graphics::Vulkan::Material droplet;
					droplet.baseColor[0] = droplet.baseColor[1] = droplet.baseColor[2] = 0.9f;
					droplet.roughness = 0.3f;
					droplet.metallic = 0.0f;
					droplet.transmission = 0.0f;
					droplet.ior = 1.333f;
					water.spraySurface = AddSimulatedSurface(dropletVertices, dropletIndices, droplet, graph.GetName(entity) + " spray");
					pending.droplets = droplets;
				}
				pendingWater.push_back(std::move(pending));
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddWater(const glm::vec3& position, Entity parent) {
				const Entity entity = graph.CreateEntity("Water", parent);
				Fox::Scene::Transform transform;
				transform.position = position;
				graph.SetWorldTransform(entity, transform);
				graph.GetRegistry().Add<Fox::Scene::WaterComponent>(entity);
				structureChanged = true;
				return entity;
			}

			void MainRayTracingScene::WaterLookChanged(Entity entity) {
				const Fox::Scene::WaterComponent* water = graph.GetRegistry().TryGet<Fox::Scene::WaterComponent>(entity);
				if (water && water->placed) {
					SetMaterial(GetSurfaceMaterial(water->surface), water->ToMaterial());
				}
			}

			bool MainRayTracingScene::SplashWater(const glm::vec3& origin, const glm::vec3& direction) {
				bool splashed = false;
				graph.GetRegistry().Each<Fox::Scene::WaterComponent>([&](Entity, Fox::Scene::WaterComponent& water) {
					if (splashed || !water.placed || !water.splashOnClick || !water.simulation || std::abs(direction.y) < 1e-6f) {
						return;
					}
					// Where the ray meets the water's level, if that is over it
					const auto& grid = water.simulation->GetGrid();
					const float t = (grid.origin.y - origin.y) / direction.y;
					if (t <= 0.0f) {
						return;
					}
					const glm::vec3 point = origin + direction * t;
					const float x = point.x - grid.origin.x, z = point.z - grid.origin.z;
					if (x < 0.0f || z < 0.0f || x > grid.width * grid.cellSize || z > grid.depth * grid.cellSize) {
						return;
					}
					water.simulation->AddDrop(x, z, std::max(0.8f, 4.0f * grid.cellSize), 0.2f);
					splashed = true;
				});
				return splashed;
			}

			void MainRayTracingScene::UpdateWater(float dt) {
				frameTime = dt;
				auto& registry = graph.GetRegistry();
				std::uniform_real_distribution<float> unit(0.0f, 1.0f);
				bool moved = false;
				registry.Each<Fox::Scene::WaterComponent>([&](Entity entity, Fox::Scene::WaterComponent& water) {
					// Turned on or off (itself or what it is under): placed anew
					const bool wanted = water.enabled && graph.IsActiveInHierarchy(entity);
					if (wanted != water.placed) {
						if (water.placed) {
							SetObjectVisible(GetSurfaceObject(water.surface), false);
							if (water.hasSpray) {
								SetObjectVisible(GetSurfaceObject(water.spraySurface), false);
							}
							water.simulation = nullptr;
							water.placed = false;
						}
						structureChanged = true;
						return;
					}
					if (!water.placed || !water.simulation) {
						return;
					}
					if (registry.Get<Fox::Scene::WorldTransformComponent>(entity).matrix != water.placedMatrix) {
						moved = true;
					}

					// Rain, and the gusts of the wind (cat's paws): little dips that the water spreads as waves. Their
					// number follows a Poisson process over the water's area.
					const auto& grid = water.simulation->GetGrid();
					const float area = grid.width * grid.cellSize * grid.depth * grid.cellSize;
					auto scatter = [&](float rate, float radius, float amount) {
						float expected = rate * area * dt;
						while (expected > 0.0f) {
							if (unit(waterRandom) < std::min(expected, 1.0f)) {
								water.simulation->AddDrop(unit(waterRandom) * grid.width * grid.cellSize, unit(waterRandom) * grid.depth * grid.cellSize,
									radius * (0.6f + 0.8f * unit(waterRandom)), amount * (0.5f + unit(waterRandom)));
							}
							expected -= 1.0f;
						}
					};
					scatter(water.rain, water.dropSize, water.dropStrength);
					// Wind ruffles the water in proportion to the square of its speed (its pressure on the surface)
					const float wind = water.windSpeed;
					scatter(0.004f * wind * wind * water.gust, std::max(1.5f, 6.0f * grid.cellSize), 0.0004f * wind * wind);
				});

				// Moved water is placed anew once it has stayed put for a moment (not on every step of a drag)
				if (moved) {
					waterMovedFor = 0.0f;
				}
				else if (waterMovedFor >= 0.0f) {
					waterMovedFor += dt;
					if (waterMovedFor > 0.3f) {
						waterMovedFor = -1.0f;
						structureChanged = true;
					}
				}
			}

			void MainRayTracingScene::RecordSimulations(VkCommandBuffer cmd, uint32_t frameSlot) {
				graph.GetRegistry().Each<Fox::Scene::WaterComponent>([&](Entity, Fox::Scene::WaterComponent& water) {
					if (!water.placed || !water.simulation) {
						return;
					}
					// Wind stress over the water's density: (air density / water density) * drag coefficient * speed^2
					const float stress = (1.2f / 1000.0f) * 1.3e-3f * water.windSpeed * water.windSpeed;
					const float angle = glm::radians(water.windDirection);
					Fox::Scene::WaterSimulation::Parameters parameters;
					parameters.viscosity = water.viscosity;
					parameters.friction = water.friction;
					parameters.wind = glm::vec2(std::cos(angle), std::sin(angle)) * stress;
					parameters.gust = water.gust;
					parameters.windSpeed = water.windSpeed;
					parameters.windAngle = angle;
					parameters.foamAmount = water.foam;
					parameters.foamLifetime = water.foamLifetime;
					parameters.sprayAmount = water.spray;
					parameters.dropletSize = water.dropletSize;
					water.simulation->Record(cmd, frameTime, parameters, frameSlot);
					MarkSurfaceDeformed(water.surface);
					if (water.hasSpray) {
						MarkSurfaceDeformed(water.spraySurface, true); // Droplets fly far: built anew every frame
					}
				});
			}

			// --- Physics ---

			namespace {
				// Position, rotation and scale of a world matrix (no shear)
				void DecomposeWorld(const glm::mat4& m, glm::vec3& position, glm::quat& rotation, glm::vec3& scale) {
					position = glm::vec3(m[3]);
					scale = glm::vec3(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])));
					const glm::mat3 r(glm::vec3(m[0]) / std::max(scale.x, 1e-8f), glm::vec3(m[1]) / std::max(scale.y, 1e-8f),
						glm::vec3(m[2]) / std::max(scale.z, 1e-8f));
					rotation = glm::normalize(glm::quat_cast(r));
				}

				bool SameMatrix(const glm::mat4& a, const glm::mat4& b) {
					for (int c = 0; c < 4; c++) {
						if (glm::any(glm::greaterThan(glm::abs(a[c] - b[c]), glm::vec4(1e-4f)))) {
							return false;
						}
					}
					return true;
				}
			}

			bool MainRayTracingScene::DescribeBody(Entity entity, const Fox::Scene::PhysicsComponent& component, Fox::Scene::PhysicsWorld::BodyDesc& desc) {
				using PhysicsComponent = Fox::Scene::PhysicsComponent;
				using World = Fox::Scene::PhysicsWorld;
				glm::vec3 scale;
				DecomposeWorld(graph.GetWorldMatrix(entity), desc.position, desc.rotation, scale);
				const float widest = std::max({ scale.x, scale.y, scale.z });

				desc.motion = component.motion == PhysicsComponent::Motion::Static ? World::Motion::Static
					: component.motion == PhysicsComponent::Motion::Kinematic ? World::Motion::Kinematic : World::Motion::Dynamic;
				desc.mass = component.mass;
				desc.density = std::max(component.density, 1.0f);
				desc.friction = component.friction;
				desc.restitution = component.restitution;
				desc.linearDamping = component.linearDamping;
				desc.angularDamping = component.angularDamping;
				desc.continuous = component.continuous;
				desc.layer = std::clamp(component.layer, 0, World::LAYER_COUNT - 1);
				desc.trigger = component.trigger;
				desc.user = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
				desc.center = component.center * scale;
				desc.halfExtents = component.halfExtents * scale;
				desc.radius = component.radius * widest;
				desc.halfHeight = component.halfHeight * scale.y;

				// What the entity shows decides the automatic shape
				const MeshComponent* mesh = graph.GetRegistry().TryGet<MeshComponent>(entity);
				PhysicsComponent::Shape shape = component.shape;
				if (shape == PhysicsComponent::Shape::Auto) {
					if (mesh && mesh->type == MeshComponent::Type::Sphere) {
						shape = PhysicsComponent::Shape::Sphere;
						desc.radius = mesh->radius * widest;
						desc.center = glm::vec3(0.0f);
					}
					else if (mesh && mesh->type == MeshComponent::Type::Model) {
						shape = component.motion == PhysicsComponent::Motion::Static ? PhysicsComponent::Shape::Mesh : PhysicsComponent::Shape::ConvexHull;
					}
					else if (mesh && mesh->type == MeshComponent::Type::Tree && mesh->tree) {
						// The trunk: a capsule as thick as its middle, standing on the entity's origin
						const auto& trunk = mesh->tree->shape;
						const float horizontal = std::max(scale.x, scale.z);
						shape = PhysicsComponent::Shape::Capsule;
						desc.radius = std::max(0.02f, trunk.trunkRadius * 0.5f * (1.0f + trunk.trunkTaper) * horizontal);
						const float height = trunk.trunkLength * scale.y;
						desc.halfHeight = std::max(0.01f, 0.5f * height - desc.radius);
						desc.center = glm::vec3(0.0f, 0.5f * height, 0.0f);
					}
					else {
						shape = PhysicsComponent::Shape::Box;
					}
				}

				switch (shape) {
				case PhysicsComponent::Shape::Sphere: desc.shape = World::ShapeType::Sphere; break;
				case PhysicsComponent::Shape::Capsule: desc.shape = World::ShapeType::Capsule; break;
				case PhysicsComponent::Shape::ConvexHull:
				case PhysicsComponent::Shape::Mesh: {
					if (!mesh || mesh->type != MeshComponent::Type::Model || mesh->file.empty()) {
						desc.shape = World::ShapeType::Box; // Nothing to take the points from
						break;
					}
					// From the model without displacement (its relief is far finer than collisions need), its parts where
					// its glTF nodes put them, as it is rendered
					const ModelShape* model = ModelShapeOf(mesh->file);
					if (!model) {
						std::cout << "Physics: " << mesh->file << " could not be loaded, using a box\n";
						desc.shape = World::ShapeType::Box;
						break;
					}
					const bool triangles = shape == PhysicsComponent::Shape::Mesh;
					desc.shape = triangles ? World::ShapeType::Mesh : World::ShapeType::ConvexHull;
					desc.points.reserve(model->positions.size());
					for (const glm::vec3& p : model->positions) {
						desc.points.push_back(p * scale);
					}
					if (triangles) {
						desc.indices = model->indices;
					}
					char key[96];
					std::snprintf(key, sizeof(key), "|%d|%.4f|%.4f|%.4f", triangles ? 1 : 0, scale.x, scale.y, scale.z);
					desc.cacheKey = mesh->file + key;
					break;
				}
				default: desc.shape = World::ShapeType::Box; break;
				}
				return true;
			}

			bool MainRayTracingScene::AddPlayingBody(Entity entity, Fox::Scene::PhysicsComponent& component) {
				Fox::Scene::PhysicsWorld::BodyDesc desc;
				DescribeBody(entity, component, desc);
				// An agent walks: its body follows it and pushes, as a kinematic one
				const auto* agent = graph.GetRegistry().TryGet<Fox::Scene::NavAgentComponent>(entity);
				const bool walks = agent && agent->enabled;
				if (walks && desc.motion == Fox::Scene::PhysicsWorld::Motion::Dynamic) {
					desc.motion = Fox::Scene::PhysicsWorld::Motion::Kinematic;
				}
				std::string bodyError;
				const uint32_t body = physics->AddBody(desc, bodyError);
				if (body == Fox::Scene::PhysicsWorld::INVALID_BODY) {
					std::cout << "Physics: no body for " << graph.GetName(entity) << " (" << bodyError << ")\n";
					failedBodies.insert(entity);
					return false;
				}
				component.body = body;
				PlayingBody playing;
				playing.entity = entity;
				playing.body = body;
				playing.motion = walks && component.motion == Fox::Scene::PhysicsComponent::Motion::Dynamic
					? Fox::Scene::PhysicsComponent::Motion::Kinematic : component.motion;
				glm::vec3 position;
				glm::quat rotation;
				DecomposeWorld(graph.GetWorldMatrix(entity), position, rotation, playing.scale);
				playing.written = graph.GetWorldMatrix(entity);
				const auto info = physics->GetBodyInfo(body);
				playing.extent = std::max(info.extent, 0.05f);
				playing.floats = component.floats && !component.trigger && component.motion == Fox::Scene::PhysicsComponent::Motion::Dynamic;
				playing.waterDrag = component.waterDrag;
				playing.buoyancy = info.mass > 0.0f && info.volume > 0.0f ? 1000.0f / (info.mass / info.volume) : 1.0f;
				playingBodies.push_back(playing);
				return true;
			}

			bool MainRayTracingScene::StartPhysics(std::string& error) {
				if (physicsPlaying) {
					return true;
				}
				if (!physics) {
					physics = std::make_unique<Fox::Scene::PhysicsWorld>();
				}
				physics->Reset();
				playingBodies.clear();
				playSnapshot.clear();
				graph.UpdateTransforms();

				// The ground: the terrain as a height field, or the ground plane
				if (environment.terrain && terrainPlaced && terrain.IsCreated()) {
					const uint32_t samples = 512; // A multiple of the block size; resampled from the terrain's heights
					const float size = terrain.settings.size;
					const float spacing = size / static_cast<float>(samples - 1);
					const glm::vec2 corner(terrain.settings.position.x - 0.5f * size, terrain.settings.position.z - 0.5f * size);
					std::vector<float> heights(static_cast<size_t>(samples) * samples);
					for (uint32_t z = 0; z < samples; z++) {
						for (uint32_t x = 0; x < samples; x++) {
							float y = environment.groundHeight;
							terrain.HeightAt(corner.x + x * spacing, corner.y + z * spacing, y);
							heights[static_cast<size_t>(z) * samples + x] = y;
						}
					}
					physics->AddHeightField(heights, samples, corner, spacing);
				}
				else if (environment.ground) {
					physics->AddGroundPlane(environment.groundHeight);
				}

				// Every entity's transform, to put back when it stops
				for (Entity entity : graph.DepthFirst()) {
					if (graph.HasTransform(entity)) {
						playSnapshot.emplace_back(entity, graph.GetLocalTransform(entity));
					}
				}

				// The bodies
				size_t failed = 0;
				failedBodies.clear();
				graph.GetRegistry().Each<Fox::Scene::PhysicsComponent>([&](Entity entity, Fox::Scene::PhysicsComponent& component) {
					component.body = Fox::Scene::PhysicsWorld::INVALID_BODY;
					if (component.enabled && graph.IsActiveInHierarchy(entity) && !AddPlayingBody(entity, component)) {
						failed++;
					}
				});
				physics->Optimize();
				physicsPlaying = true;
				StartNavigation();
				physics->TakeContactEvents(); // Nothing touched yet
				// Growing trees start from a sprout
				graph.GetRegistry().Each<MeshComponent>([](Entity, MeshComponent& mesh) {
					if (mesh.type == MeshComponent::Type::Tree && mesh.tree && mesh.tree->growth.enabled) {
						mesh.tree->growthTime = 0.0f;
						mesh.tree->growthPaused = false;
					}
				});
				std::cout << "Physics: playing, " << playingBodies.size() << " bodies" << (failed ? " (some failed, see above)" : "") << "\n";
				size_t agents = 0;
				graph.GetRegistry().Each<Fox::Scene::NavAgentComponent>([&](Entity, Fox::Scene::NavAgentComponent& agent) {
					agents += agent.enabled ? 1 : 0;
				});
				(void)agents;
				(void)error;
				return true;
			}

			void MainRayTracingScene::StopPhysics() {
				if (!physicsPlaying) {
					return;
				}
				physicsPlaying = false;
				for (const auto& [entity, transform] : playSnapshot) {
					if (graph.IsValid(entity) && graph.HasTransform(entity)) {
						graph.SetLocalTransform(entity, transform);
					}
				}
				graph.GetRegistry().Each<Fox::Scene::PhysicsComponent>([](Entity, Fox::Scene::PhysicsComponent& component) {
					component.body = Fox::Scene::PhysicsWorld::INVALID_BODY;
				});
				playSnapshot.clear();
				playingBodies.clear();
				if (physics) {
					physics->Reset();
				}
				graph.UpdateTransforms();
				InvalidatePaths(); // Planned again from where they are back at
				std::cout << "Physics: stopped (everything is back where it was)\n";
			}

			void MainRayTracingScene::UpdatePhysics(float dt) {
				if (!physics) {
					return;
				}
				using Motion = Fox::Scene::PhysicsComponent::Motion;

				// Entities that got a body while it plays (created by hand or by the game, or switched on) join in
				bool added = false;
				graph.GetRegistry().Each<Fox::Scene::PhysicsComponent>([&](Entity entity, Fox::Scene::PhysicsComponent& component) {
					if (component.body == Fox::Scene::PhysicsWorld::INVALID_BODY && component.enabled && graph.IsActiveInHierarchy(entity)
						&& !failedBodies.count(entity)) {
						added |= AddPlayingBody(entity, component);
					}
				});
				if (added) {
					physics->Optimize();
				}

				// Entities gone or switched off leave; dynamic ones moved by hand (the gizmo) go where they were put,
				// thrown with the speed they were moved at
				for (size_t i = 0; i < playingBodies.size();) {
					PlayingBody& playing = playingBodies[i];
					const auto* component = graph.IsValid(playing.entity) ? graph.GetRegistry().TryGet<Fox::Scene::PhysicsComponent>(playing.entity) : nullptr;
					if (!component || !component->enabled || component->body != playing.body || !graph.IsActiveInHierarchy(playing.entity)) {
						physics->RemoveBody(playing.body);
						playingBodies.erase(playingBodies.begin() + i);
						continue;
					}
					const glm::mat4 world = graph.GetWorldMatrix(playing.entity);
					if (playing.motion == Motion::Dynamic && !SameMatrix(world, playing.written)) {
						glm::vec3 position, scale;
						glm::quat rotation;
						DecomposeWorld(world, position, rotation, scale);
						const glm::vec3 moved = (position - glm::vec3(playing.written[3])) / std::max(dt, 1e-3f);
						physics->SetTransform(playing.body, position, rotation);
						physics->SetVelocity(playing.body, glm::clamp(moved, glm::vec3(-20.0f), glm::vec3(20.0f)), glm::vec3(0.0f));
						playing.written = world;
					}
					i++;
				}

				// The water bodies to float in
				std::vector<Fox::Scene::WaterSimulation*> waters;
				graph.GetRegistry().Each<Fox::Scene::WaterComponent>([&](Entity, Fox::Scene::WaterComponent& water) {
					if (water.placed && water.simulation) {
						waters.push_back(water.simulation.get());
					}
				});
				auto sampleWater = [&](const glm::vec3& at, glm::vec3& point, glm::vec3& normal, glm::vec3& flow, Fox::Scene::WaterSimulation*& in) {
					for (auto* water : waters) {
						float height;
						glm::vec2 velocity;
						if (water->SampleSurface(at.x, at.z, height, normal, velocity)) {
							point = glm::vec3(at.x, height, at.z);
							flow = glm::vec3(velocity.x, 0.0f, velocity.y);
							in = water;
							return true;
						}
					}
					return false;
				};

				// The collision matrix as it is now (it can be edited while playing)
				{
					uint32_t masks[Fox::Scene::PhysicsWorld::LAYER_COUNT];
					for (int i = 0; i < Fox::Scene::PhysicsWorld::LAYER_COUNT; i++) {
						masks[i] = environment.layers.collides[i];
					}
					physics->SetLayerMatrix(masks);
				}

				physics->Step(dt, [&](float step) {
					for (PlayingBody& playing : playingBodies) {
						if (playing.motion == Motion::Kinematic) {
							// It follows its entity (the gizmo, or its parent moving it)
							glm::vec3 position, scale;
							glm::quat rotation;
							DecomposeWorld(graph.GetWorldMatrix(playing.entity), position, rotation, scale);
							physics->MoveKinematic(playing.body, position, rotation, step);
						}
						else if (playing.motion == Motion::Dynamic && playing.floats && !waters.empty()) {
							glm::vec3 position, point, normal, flow;
							glm::quat rotation;
							Fox::Scene::WaterSimulation* water = nullptr;
							physics->GetTransform(playing.body, position, rotation);
							if (sampleWater(position, point, normal, flow, water)) {
								physics->ApplyBuoyancy(playing.body, point, normal, playing.buoyancy, playing.waterDrag, 0.4f * playing.waterDrag, flow, step);
							}
						}
					}
				});

				// Where the dynamic bodies are now; splashes where they hit the water, a wake where they move through it
				for (PlayingBody& playing : playingBodies) {
					if (playing.motion != Motion::Dynamic) {
						continue;
					}
					glm::vec3 position;
					glm::quat rotation;
					physics->GetTransform(playing.body, position, rotation);
					Fox::Scene::Transform world;
					world.position = position;
					world.rotation = rotation;
					world.scale = playing.scale;
					graph.SetWorldTransform(playing.entity, world);
					playing.written = world.Matrix();

					if (waters.empty()) {
						continue;
					}
					glm::vec3 point, normal, flow;
					Fox::Scene::WaterSimulation* water = nullptr;
					const bool over = sampleWater(position, point, normal, flow, water);
					const float submerged = over ? physics->SubmergedFraction(playing.body, point, normal) : 0.0f;
					const bool inWater = submerged > 0.0f;
					if (inWater) {
						const glm::vec3 velocity = physics->GetLinearVelocity(playing.body);
						const auto& grid = water->GetGrid();
						const float x = position.x - grid.origin.x, z = position.z - grid.origin.z;
						const float radius = std::max(playing.extent * 1.5f, 2.0f * grid.cellSize);
						if (!playing.inWater && velocity.y < -1.0f) {
							// Hitting the water: a dip as deep as it is hard, and the foam and spray that come with it
							water->AddDrop(x, z, radius, std::min(0.5f, 0.04f * -velocity.y * (0.5f + playing.extent)));
						}
						const glm::vec2 through(velocity.x - flow.x, velocity.z - flow.z);
						const float speed = glm::length(through);
						playing.wakeTimer += dt;
						if (submerged < 0.95f && speed > 0.8f && playing.wakeTimer > 0.12f) {
							water->AddDrop(x, z, radius, std::min(0.15f, 0.012f * speed * (0.5f + playing.extent)));
							playing.wakeTimer = 0.0f;
						}
					}
					playing.inWater = inWater;
				}

				// Last: scripts hear what touched and parted (they may destroy or add things: bodies follow next frame)
				DispatchContacts(physics->TakeContactEvents());
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddPhysicsBall(const glm::vec3& position) {
				SurfaceMaterial material{ glm::vec3(0.75f, 0.12f, 0.08f), 0.35f };
				const Entity entity = AddSphereObject(position, 0.4f, material);
				graph.SetName(entity, "Ball");
				auto& body = graph.GetRegistry().Add<Fox::Scene::PhysicsComponent>(entity);
				body.restitution = 0.5f;
				body.density = 300.0f;
				return entity;
			}

			MainRayTracingScene::Entity MainRayTracingScene::AddPhysicsCrate(const glm::vec3& position) {
				const Entity entity = AddModel("Models/box.glb", false, position);
				graph.SetName(entity, "Box");
				// Scaled to a metre across, whatever size the model is
				if (const ModelShape* model = ModelShapeOf("Models/box.glb")) {
					const glm::vec3 extent = model->high - model->low;
					const float size = std::max({ extent.x, extent.y, extent.z });
					if (size > 1e-4f) {
						Fox::Scene::Transform transform = graph.GetLocalTransform(entity);
						transform.scale = glm::vec3(1.0f / size);
						graph.SetLocalTransform(entity, transform);
					}
				}
				auto& body = graph.GetRegistry().Add<Fox::Scene::PhysicsComponent>(entity);
				body.density = 600.0f; // Wood
				return entity;
			}

			size_t MainRayTracingScene::CountUsers(const Tree* tree) const {
				size_t count = 0;
				graph.GetRegistry().Each<MeshComponent>([&](Entity, const MeshComponent& mesh) {
					count += mesh.type == MeshComponent::Type::Tree && mesh.tree.get() == tree ? 1 : 0;
				});
				return count;
			}
		}
	}
}
