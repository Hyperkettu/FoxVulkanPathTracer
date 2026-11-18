#include "FoxRenderer.h"

#include <filesystem>

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
					std::cout << "Scene: no " << DEFAULT_SCENE_FILE << ", starting with an empty scene\n";
				}

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

				// Static content: models and spheres, then the ground and sun
				for (Entity entity : entities) {
					MeshComponent* mesh = registry.TryGet<MeshComponent>(entity);
					if (!mesh) {
						continue;
					}
					mesh->placed = false;
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

				terrainPlaced = false;
				if (environment.terrain) {
					PlaceTerrain();
				}
				else if (environment.ground) {
					AddGroundPlane(environment.groundHeight, 2000.0f, environment.groundMaterial.ToMaterial());
				}
				if (environment.sun) {
					AddSun(environment.sunDirection, environment.sunColor * environment.sunIntensity, environment.sunAngularRadius);
				}
				objectEntities.resize(GetObjectCount());

				Build();
				structureChanged = false;

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
					}
					const bool show = mesh.enabled && graph.IsActiveInHierarchy(entity);
					if (show != IsObjectVisible(mesh.object)) {
						SetObjectVisible(mesh.object, show);
						objectsChanged = true;
					}
					mesh.shown = show;
				});
			}

			void MainRayTracingScene::ClearContent() {
				graph.Clear();
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
				}
				graph.DestroyEntity(entity);
			}

			MainRayTracingScene::Entity MainRayTracingScene::DuplicateEntity(Entity entity) {
				const Entity copy = graph.Duplicate(entity);
				if (!graph.IsValid(copy)) {
					return copy;
				}
				auto& registry = graph.GetRegistry();
				for (Entity e : graph.Subtree(copy)) {
					if (MeshComponent* mesh = registry.TryGet<MeshComponent>(e)) {
						// A placement of its own, and a tree of its own
						mesh->placed = false;
						if (mesh->tree) {
							mesh->tree = mesh->tree->CloneSettings();
						}
						MeshChanged(mesh, nullptr);
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

			void MainRayTracingScene::Update(float dt) {
				time += dt;

				// The scene graph's world transforms, and the meshes placed and shown as their entities are
				graph.UpdateTransforms();
				SyncMeshes();

				// Wind in every tree; it blows in world space, wherever a tree has been moved to
				graph.GetRegistry().Each<MeshComponent>([&](Entity, MeshComponent& mesh) {
					Tree* tree = mesh.type == MeshComponent::Type::Tree ? mesh.tree.get() : nullptr;
					if (!tree || !tree->placed || !tree->skeleton || !mesh.placed || tree->handle.object >= GetObjectCount()) {
						return;
					}
					tree->skeleton->GetSettings() = tree->wind;
					tree->skeleton->Update(dt, time, GetObjectTransform(tree->handle.object).Matrix());
					SetJointMatrices(tree->handle.skinnedMesh, tree->skeleton->GetSkinMatrices());
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
				tree.skeleton = std::make_unique<Fox::Animation::WindSkeleton>(tree.mesh->bones, tree.wind);
				tree.regenerate = false;
				tree.generationSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
			}

			bool MainRayTracingScene::ApplyTreeChanges(std::string& error) {
				treesChanged = false;
				graph.UpdateTransforms();
				auto& registry = graph.GetRegistry();
				try {
					const std::vector<Entity> entities = GetTreeEntities();
					std::vector<DynamicMesh> meshes;
					for (Entity entity : entities) {
						Tree& tree = *GetTree(entity);
						if (tree.regenerate || !tree.mesh) {
							GenerateTreeMesh(tree);
						}

						DynamicMesh mesh;
						mesh.mesh = tree.mesh.get();
						mesh.materials = MaterialsFor(tree.look);
						mesh.transform = graph.GetWorldTransform(entity);
						mesh.name = graph.GetName(entity);
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

					for (size_t i = 0; i < entities.size(); i++) {
						MeshComponent& mesh = registry.Get<MeshComponent>(entities[i]);
						mesh.tree->handle = handles[i];
						mesh.tree->placed = true;
						mesh.placed = true;
						mesh.object = handles[i].object;
						mesh.placedMatrix = registry.Get<Fox::Scene::WorldTransformComponent>(entities[i]).matrix;
						objectEntities[mesh.object] = entities[i];
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
		}
	}
}
