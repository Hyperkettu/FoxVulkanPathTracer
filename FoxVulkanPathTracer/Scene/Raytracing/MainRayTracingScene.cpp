#include "FoxRenderer.h"

namespace Fox {

	namespace Scene {

		namespace RayTracing {

			namespace {
				// The cabin model's foundation ends at this height; the ground is placed there
				const float GROUND_HEIGHT = -3.73f;

				// The sun of Textures/environment_map.hdr sits on the horizon in this direction.
				// The analytic sun is raised a little for longer-reaching, less grazing light.
				const glm::vec3 SUN_DIRECTION(0.876f, 0.22f, 0.479f);

				// Golden hour: warm, and not much stronger than the sky itself
				const glm::vec3 SUN_IRRADIANCE = glm::vec3(1.0f, 0.58f, 0.30f) * 18.0f;
				const float SUN_ANGULAR_RADIUS = 1.2f; // Degrees. Larger than the real sun for softer shadows
			}

			void MainRayTracingScene::Initialize() {
				using Fox::Graphics::Geometry::Vulkan::Mesh;

				const glm::vec3 up(0.0f, 1.0f, 0.0f);

				// The meshes are only needed while the scene is assembled: AddMesh copies their
				// geometry, and the textures are owned by the TextureManager.
				Mesh cabin(device, physicalDevice, queue, queueFamily);

				// Displacement: the cabin file has no height maps (glTF cannot carry them), so the relief of
				// its stones, logs and shingles is reconstructed from the normal maps. To use real height maps
				// instead, assign them per material name:
				//   cabinDisplacement.heightMaps["concrete_pebbles.001"] = { "Textures/pebbles_height.png", 0.05f };
				Fox::Graphics::Geometry::DisplacementSettings cabinDisplacement;
				cabinDisplacement.enabled = true; // Default budget: up to 4 M triangles, ~1 ms of tracing at 4K on an RTX 5090

				cabin.LoadFromGLTF("Models/cabin.glb", cabinDisplacement);

				Mesh box(device, physicalDevice, queue, queueFamily);
				box.LoadFromGLTF("Models/box.glb");

				glm::mat4 cabinTransform = glm::translate(glm::mat4(1.0f), glm::vec3(20.0f, 0.0f, 0.0f));
				cabinTransform = glm::rotate(cabinTransform, glm::radians(90.0f), up);
				AddMesh(cabin, cabinTransform, "Cabin");

				// The box model's floor is at y = -0.55: lower it so it rests on the ground
				glm::mat4 boxTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, GROUND_HEIGHT + 0.55f, 0.0f));
				boxTransform = glm::rotate(boxTransform, glm::radians(25.0f), up);
				AddMesh(box, boxTransform, "Box");

				// Second cabin: the same mesh instanced with another transform
				glm::mat4 secondCabinTransform = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), up);
				secondCabinTransform = glm::translate(secondCabinTransform, glm::vec3(50.0f, 0.0f, 50.0f));
				AddMesh(cabin, secondCabinTransform, "Second cabin");

				// Dark, slightly glossy ground (think wet asphalt): picks up reflections of the sky and the buildings
				Fox::Graphics::Vulkan::Material ground;
				ground.baseColor[0] = 0.07f;
				ground.baseColor[1] = 0.07f;
				ground.baseColor[2] = 0.08f;
				ground.roughness = 0.12f;
				ground.metallic = 0.0f;
				AddGroundPlane(GROUND_HEIGHT, 2000.0f, ground);

				// Solid glass ball resting on the ground in front of the camera. It refracts the scene behind it
				// upside down, and the faint green of thick window glass builds up towards its centre.
				Fox::Graphics::Vulkan::Material glass;
				glass.roughness = 0.0f;
				glass.metallic = 0.0f;
				glass.transmission = 1.0f;
				glass.ior = 1.5f;
				const float glassAttenuation[3] = { 0.80f, 0.94f, 0.90f };
				glass.SetAttenuation(glassAttenuation, 2.0f); // Colour left after 2 m of glass
				const float ballRadius = 1.2f;
				AddSphere(glm::vec3(21.0f, GROUND_HEIGHT + ballRadius, 10.0f), ballRadius, glass, "Glass ball");

				AddSun(SUN_DIRECTION, SUN_IRRADIANCE, SUN_ANGULAR_RADIUS);

				Build();

				// Trees are dynamic: generated and rebuilt while the scene renders (the editor changes them)
				textureLibrary = std::make_unique<Fox::Scene::TextureLibrary>(device, physicalDevice, commandPool->Get(), queue);
				AddTree(glm::vec3(14.0f, GROUND_HEIGHT, 12.0f));
				std::string error;
				if (!ApplyTreeChanges(error)) {
					throw std::runtime_error(error);
				}
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
				return GROUND_HEIGHT;
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
