#include "FoxRenderer.h"

#include <filesystem>
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

				// C# scripts (Scripts/*.cs)
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
				const size_t staticBudget = MAX_INSTANCES / 2;
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
						AddSphere(glm::vec3(0.0f), mesh->radius, mesh->material.ToMaterial(), name);
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
				SyncLights();
				UpdateWater(step);

				// Wind in every tree; it blows in world space, wherever a tree has been moved to. Instances of a tree
				// share its deformation: it is computed once, for its first placement.
				std::unordered_set<const Tree*> blown;
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
					tree->skeleton->GetSettings() = tree->wind;
					tree->skeleton->SetGrowth(tree->GrowthProgress(), tree->growth);
					tree->skeleton->Update(step, time, GetObjectTransform(tree->handle.object).Matrix());
					SetJointMatrices(tree->handle.skinnedMesh, tree->skeleton->GetSkinMatrices());
					SetSkinGrowth(tree->handle.skinnedMesh, tree->skeleton->GetLeafFront(), tree->skeleton->GetLeafSpan());
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
					// From the model without displacement: its relief is far finer than collisions need
					Fox::Graphics::Geometry::Vulkan::Mesh* model = nullptr;
					try {
						model = &LoadModel(mesh->file, false);
					}
					catch (const std::exception& e) {
						std::cout << "Physics: " << mesh->file << " could not be loaded (" << e.what() << "), using a box\n";
					}
					if (!model) {
						desc.shape = World::ShapeType::Box;
						break;
					}
					const bool triangles = shape == PhysicsComponent::Shape::Mesh;
					desc.shape = triangles ? World::ShapeType::Mesh : World::ShapeType::ConvexHull;
					desc.points.reserve(model->GetVertices().size());
					for (const auto& vertex : model->GetVertices()) {
						desc.points.emplace_back(vertex.position[0] * scale.x, vertex.position[1] * scale.y, vertex.position[2] * scale.z);
					}
					if (triangles) {
						desc.indices.assign(model->GetIndices().begin(), model->GetIndices().end());
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
				playing.floats = component.floats && component.motion == Fox::Scene::PhysicsComponent::Motion::Dynamic;
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
				try {
					glm::vec3 low(1e30f), high(-1e30f);
					for (const auto& vertex : LoadModel("Models/box.glb", false).GetVertices()) {
						const glm::vec3 p(vertex.position[0], vertex.position[1], vertex.position[2]);
						low = glm::min(low, p);
						high = glm::max(high, p);
					}
					const glm::vec3 extent = high - low;
					const float size = std::max({ extent.x, extent.y, extent.z });
					if (size > 1e-4f) {
						Fox::Scene::Transform transform = graph.GetLocalTransform(entity);
						transform.scale = glm::vec3(1.0f / size);
						graph.SetLocalTransform(entity, transform);
					}
				}
				catch (const std::exception&) {
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
