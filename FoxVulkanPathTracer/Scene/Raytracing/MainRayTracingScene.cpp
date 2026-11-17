#include "FoxRenderer.h"

#include <filesystem>

namespace Fox {

	namespace Scene {

		namespace RayTracing {

			Fox::Graphics::Vulkan::Material MainRayTracingScene::SurfaceMaterial::ToMaterial() const {
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

			void MainRayTracingScene::SyncTransforms() {
				for (Model& model : models) {
					if (model.placed && model.object < GetObjectCount()) {
						model.transform = GetObjectTransform(model.object);
					}
				}
				for (Sphere& sphere : spheres) {
					if (sphere.placed && sphere.object < GetObjectCount()) {
						sphere.transform = GetObjectTransform(sphere.object);
					}
				}
				for (Tree& tree : trees) {
					if (tree.placed && tree.handle.object < GetObjectCount()) {
						tree.transform = GetObjectTransform(tree.handle.object);
					}
				}
			}

			bool MainRayTracingScene::RebuildScene(std::string& error) {
				SyncTransforms();
				Clear();
				std::string problems;

				// Static content: models, spheres, ground and sun
				for (Model& model : models) {
					model.placed = false;
					try {
						AddMesh(LoadModel(model.file, model.displacement), model.transform.Matrix(), model.name);
						model.object = GetObjectCount() - 1;
						model.placed = true;
						SetObjectTransform(model.object, model.transform); // Exactly, not decomposed from the matrix
					}
					catch (const std::exception& e) {
						problems += std::string(problems.empty() ? "" : "; ") + model.file + ": " + e.what();
					}
				}

				for (Sphere& sphere : spheres) {
					AddSphere(glm::vec3(0.0f), sphere.radius, sphere.material.ToMaterial(), sphere.name);
					sphere.object = GetObjectCount() - 1;
					sphere.placed = true;
					SetObjectTransform(sphere.object, sphere.transform);
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

				Build();
				structureChanged = false;

				// Dynamic content: trees
				for (Tree& tree : trees) {
					tree.placed = false;
				}
				std::string treeError;
				if (!ApplyTreeChanges(treeError)) {
					problems += std::string(problems.empty() ? "" : "; ") + treeError;
				}

				error = problems;
				return problems.empty();
			}

			void MainRayTracingScene::ClearContent() {
				models.clear();
				spheres.clear();
				trees.clear();
				environment = Environment{};
				cameraManager.Clear();
				ResetTerrain();
				structureChanged = true;
			}

			size_t MainRayTracingScene::AddModel(const std::string& file, bool displacement, const glm::vec3& position) {
				Model model;
				model.file = file;
				model.displacement = displacement;
				model.name = std::filesystem::path(file).stem().string();
				model.transform.position = position;
				models.push_back(std::move(model));
				structureChanged = true;
				return models.size() - 1;
			}

			size_t MainRayTracingScene::AddSphereObject(const glm::vec3& position, float radius, const SurfaceMaterial& material) {
				Sphere sphere;
				sphere.name = "Sphere " + std::to_string(spheres.size() + 1);
				sphere.radius = radius;
				sphere.material = material;
				sphere.transform.position = position;
				spheres.push_back(std::move(sphere));
				structureChanged = true;
				return spheres.size() - 1;
			}

			bool MainRayTracingScene::RemoveObject(int object) {
				if (object < 0) {
					return false;
				}
				SyncTransforms();

				// The objects after a removed one move down an index once the scene is rebuilt; until then none of
				// them can be selected by their old index
				auto removed = [this] {
					for (Model& model : models) {
						model.placed = false;
					}
					for (Sphere& sphere : spheres) {
						sphere.placed = false;
					}
					structureChanged = true;
					return true;
				};
				for (size_t i = 0; i < models.size(); i++) {
					if (models[i].placed && static_cast<int>(models[i].object) == object) {
						models.erase(models.begin() + static_cast<std::ptrdiff_t>(i));
						return removed();
					}
				}
				for (size_t i = 0; i < spheres.size(); i++) {
					if (spheres[i].placed && static_cast<int>(spheres[i].object) == object) {
						spheres.erase(spheres.begin() + static_cast<std::ptrdiff_t>(i));
						return removed();
					}
				}
				const int tree = GetTreeOfObject(object);
				if (tree >= 0) {
					RemoveTree(static_cast<size_t>(tree));
					return true;
				}
				return false;
			}

			void MainRayTracingScene::Update(float dt) {
				time += dt;

				// Wind in every tree; it blows in world space, wherever a tree has been moved to
				for (Tree& tree : trees) {
					if (!tree.placed || !tree.skeleton) {
						continue;
					}
					tree.skeleton->GetSettings() = tree.wind;
					tree.skeleton->Update(dt, time, GetObjectTransform(tree.handle.object).Matrix());
					SetJointMatrices(tree.handle.skinnedMesh, tree.skeleton->GetSkinMatrices());
				}
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

			size_t MainRayTracingScene::AddTree(const glm::vec3& position, const Tree* copyOf) {
				Tree tree;
				if (copyOf) {
					tree.shape = copyOf->shape;
					tree.look = copyOf->look;
					tree.wind = copyOf->wind;
					tree.transform = copyOf->transform;
				}
				tree.transform.position = position;
				tree.name = "Tree " + std::to_string(trees.size() + 1);
				trees.push_back(std::move(tree));
				treesChanged = true;
				return trees.size() - 1;
			}

			void MainRayTracingScene::RemoveTree(size_t tree) {
				if (tree < trees.size()) {
					trees.erase(trees.begin() + static_cast<std::ptrdiff_t>(tree));
					treesChanged = true;
				}
			}

			int MainRayTracingScene::GetTreeOfObject(int object) const {
				for (size_t i = 0; i < trees.size(); i++) {
					if (trees[i].placed && static_cast<int>(trees[i].handle.object) == object) {
						return static_cast<int>(i);
					}
				}
				return -1;
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
				try {
					std::vector<DynamicMesh> meshes;
					for (Tree& tree : trees) {
						// Keep where the gizmos moved it
						if (tree.placed) {
							tree.transform = GetObjectTransform(tree.handle.object);
						}
						if (tree.regenerate || !tree.mesh) {
							GenerateTreeMesh(tree);
						}

						DynamicMesh mesh;
						mesh.mesh = tree.mesh.get();
						mesh.materials = MaterialsFor(tree.look);
						mesh.transform = tree.transform;
						mesh.name = tree.name;
						meshes.push_back(std::move(mesh));
					}

					const std::vector<DynamicMeshHandle> handles = RebuildDynamicMeshes(meshes);
					for (size_t i = 0; i < trees.size(); i++) {
						trees[i].handle = handles[i];
						trees[i].placed = true;
					}
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
