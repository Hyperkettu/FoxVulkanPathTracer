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

				const std::string BARK_TEXTURES = "Textures/bark/";

				// A single channel image in [0, 1], repeating in both directions
				struct GrayImage {
					int width = 0;
					int height = 0;
					std::vector<float> values;

					float At(int x, int y) const {
						x = ((x % width) + width) % width;
						y = ((y % height) + height) % height;
						return values[static_cast<size_t>(y) * width + x];
					}

					// Bilinear, at texture coordinates (texel centers at (i + 0.5) / size, like the GPU)
					float Sample(float u, float v) const {
						const float x = u * static_cast<float>(width) - 0.5f;
						const float y = v * static_cast<float>(height) - 0.5f;
						const int x0 = static_cast<int>(std::floor(x));
						const int y0 = static_cast<int>(std::floor(y));
						const float fx = x - static_cast<float>(x0);
						const float fy = y - static_cast<float>(y0);
						const float top = glm::mix(At(x0, y0), At(x0 + 1, y0), fx);
						const float bottom = glm::mix(At(x0, y0 + 1), At(x0 + 1, y0 + 1), fx);
						return glm::mix(top, bottom, fy);
					}

					// Averages factor x factor blocks (factor divides the size)
					GrayImage Downsampled(int factor) const {
						GrayImage result;
						result.width = std::max(width / factor, 1);
						result.height = std::max(height / factor, 1);
						result.values.assign(static_cast<size_t>(result.width) * result.height, 0.0f);
						const float weight = 1.0f / static_cast<float>(factor * factor);
						for (int y = 0; y < result.height * factor && y < height; y++) {
							for (int x = 0; x < result.width * factor && x < width; x++) {
								result.values[static_cast<size_t>(y / factor) * result.width + x / factor] += values[static_cast<size_t>(y) * width + x] * weight;
							}
						}
						return result;
					}
				};

				// 8 and 16 bit images, converted to one channel (grey) at full precision
				GrayImage LoadGray(const std::string& path) {
					int width = 0, height = 0, channels = 0;
					stbi_us* data = stbi_load_16(path.c_str(), &width, &height, &channels, 1);
					if (!data) {
						throw std::runtime_error("Failed to load " + path);
					}

					GrayImage image;
					image.width = width;
					image.height = height;
					image.values.resize(static_cast<size_t>(width) * height);
					for (size_t i = 0; i < image.values.size(); i++) {
						image.values[i] = static_cast<float>(data[i]) / 65535.0f;
					}
					stbi_image_free(data);
					return image;
				}
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

				// A tree built from cylinders, rigged as it grows, swaying in the wind
				{
					using Fox::Graphics::Vulkan::ShaderResourceTexture;
					auto& textureManager = Fox::Graphics::Managers::Vulkan::TextureManager::Get();
					auto addTexture = [&](std::unique_ptr<ShaderResourceTexture> texture) {
						return static_cast<int>(textureManager.AddBindlessTexture(std::move(texture)));
					};

					// Bark material from Textures/bark: colour (sRGB) and normal map as they are; roughness, metallic and
					// ambient occlusion packed into one texture the glTF way (R occlusion, G roughness, B metallic)
					Fox::Graphics::Vulkan::Material bark;
					bark.roughness = 1.0f; // Factors multiply the textures
					bark.metallic = 1.0f;
					bark.baseColorTexture = addTexture(ShaderResourceTexture::LoadFromFile(device, physicalDevice, commandPool->Get(), queue, BARK_TEXTURES + "albedo.png", true));
					bark.normalTexture = addTexture(ShaderResourceTexture::LoadFromFile(device, physicalDevice, commandPool->Get(), queue, BARK_TEXTURES + "normal.png", false));
					{
						const GrayImage occlusion = LoadGray(BARK_TEXTURES + "ao.png");
						const GrayImage roughness = LoadGray(BARK_TEXTURES + "roughness.png");
						const GrayImage metallic = LoadGray(BARK_TEXTURES + "metallic.png");
						if (roughness.width != occlusion.width || roughness.height != occlusion.height
							|| metallic.width != occlusion.width || metallic.height != occlusion.height) {
							throw std::runtime_error("The bark ao, roughness and metallic textures must have the same size");
						}

						std::vector<uint8_t> packed(occlusion.values.size() * 4);
						for (size_t i = 0; i < occlusion.values.size(); i++) {
							packed[i * 4 + 0] = static_cast<uint8_t>(std::lround(occlusion.values[i] * 255.0f));
							packed[i * 4 + 1] = static_cast<uint8_t>(std::lround(roughness.values[i] * 255.0f));
							packed[i * 4 + 2] = static_cast<uint8_t>(std::lround(metallic.values[i] * 255.0f));
							packed[i * 4 + 3] = 255;
						}
						const int occlusionRoughnessMetallic = addTexture(ShaderResourceTexture::LoadFromPixelData(device, physicalDevice, commandPool->Get(), queue,
							packed.data(), static_cast<uint32_t>(occlusion.width), static_cast<uint32_t>(occlusion.height), VK_FORMAT_R8G8B8A8_UNORM, 4));
						bark.metallicRoughnessTexture = occlusionRoughnessMetallic;
						bark.occlusionTexture = occlusionRoughnessMetallic;
					}

					// Displacement map: real relief on the trunk and branches. Averaged down to about two texels per
					// triangle edge: finer detail would only alias between the vertices (the normal map carries it).
					Fox::Graphics::Geometry::TreeSettings treeSettings;
					{
						const GrayImage displacement = LoadGray(BARK_TEXTURES + "displacement.png");
						const float texelsPerEdge = static_cast<float>(displacement.width) * treeSettings.detailEdgeLength / treeSettings.textureSize;
						int factor = 1;
						while (factor * 2 <= texelsPerEdge * 0.5f && displacement.width % (factor * 2) == 0) {
							factor *= 2;
						}
						const GrayImage heights = displacement.Downsampled(factor);
						treeSettings.height = [heights](float u, float v) { return heights.Sample(u, v); };
					}

					const Fox::Graphics::Geometry::SkinnedMesh treeMesh = Fox::Graphics::Geometry::GenerateTree(treeSettings);

					const glm::mat4 treeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(14.0f, GROUND_HEIGHT, 12.0f));
					tree = AddSkinnedMesh(treeMesh, bark, treeTransform, "Tree");
					wind = std::make_unique<Fox::Animation::WindSkeleton>(treeMesh.bones);

					std::cout << "Tree: " << treeMesh.bones.size() << " bones, " << treeMesh.indices.size() / 3 << " triangles\n";
				}

				AddSun(SUN_DIRECTION, SUN_IRRADIANCE, SUN_ANGULAR_RADIUS);

				Build();
			}

			void MainRayTracingScene::Update(float dt) {
				time += dt;

				if (wind) {
					// The wind blows in world space, wherever the tree has been moved to
					const glm::mat4 treeTransform = GetObjectTransform(GetObjectOfSkinnedMesh(tree)).Matrix();
					wind->Update(dt, time, treeTransform);
					SetJointMatrices(tree, wind->GetSkinMatrices());
				}
			}
		}
	}
}
