#include "FoxRenderer.h"

// Static member definitions
std::unique_ptr<Fox::Graphics::Managers::Vulkan::TextureManager> Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::TextureManager>::instance = nullptr;
std::once_flag Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::TextureManager>::initFlag;

uint32_t Fox::Graphics::Managers::Vulkan::TextureManager::ENVIRONMENT_MAP_TEXTURE_INDEX = ~0u;
float Fox::Graphics::Managers::Vulkan::TextureManager::ENVIRONMENT_MAP_INTENSITY = 1.0f;

namespace Fox {

	namespace Graphics {

		namespace Managers {

			namespace Vulkan {

				bool TextureManager::Initialize(
					VkDevice device,
					VkPhysicalDevice physicalDevice,
					VkExtent2D extent,
					VkFormat depthFormat,
					VkQueue graphicsQueue,
					VkCommandPool commandPool
				) {
					this->device = device;
					this->physicalDevice = physicalDevice;
					this->depthFormat = depthFormat;

					environmentUBO = std::make_unique<Fox::Graphics::Vulkan::ConstantBuffer<Fox::Graphics::Vulkan::Environment>>(device, physicalDevice, "Environment UBO");

					Resize(extent);

					const std::string environmentMapPath = "Textures/environment_map.hdr";

					ENVIRONMENT_MAP_TEXTURE_INDEX = AddBindlessTexture(
						Fox::Graphics::Vulkan::ShaderResourceTexture::LoadFromFile(device, physicalDevice, commandPool, graphicsQueue, environmentMapPath));

					BuildEnvironmentSamplingTable(environmentMapPath);

					return true;
				}

				void TextureManager::BuildEnvironmentSamplingTable(const std::string& path) {
					int width = 0, height = 0, channels = 0;
					float* pixels = stbi_loadf(path.c_str(), &width, &height, &channels, STBI_rgb);
					if (!pixels) {
						throw std::runtime_error("Failed to load environment map: " + path);
					}

					// The table works on blocks of texels: full resolution is not needed to find the bright regions
					const int blockSize = std::max(1, width / 1024);
					environmentTableWidth = static_cast<uint32_t>(width / blockSize);
					environmentTableHeight = static_cast<uint32_t>(height / blockSize);
					const size_t cellCount = static_cast<size_t>(environmentTableWidth) * environmentTableHeight;

					// Importance of a cell = its luminance times the solid angle it covers. Rows near the poles
					// of the equirectangular map are stretched, hence the sin(theta) factor.
					std::vector<double> importance(cellCount, 0.0);
					double total = 0.0;

					for (uint32_t cellY = 0; cellY < environmentTableHeight; cellY++) {
						const double theta = (cellY + 0.5) / environmentTableHeight * 3.14159265358979;
						const double sinTheta = std::sin(theta);

						for (uint32_t cellX = 0; cellX < environmentTableWidth; cellX++) {
							double luminance = 0.0;

							for (int y = 0; y < blockSize; y++) {
								const float* row = pixels + (static_cast<size_t>(cellY * blockSize + y) * width + cellX * blockSize) * 3;
								for (int x = 0; x < blockSize; x++) {
									// Same clamp as the texture upload (half float range)
									const float r = std::min(row[x * 3 + 0], 65504.0f);
									const float g = std::min(row[x * 3 + 1], 65504.0f);
									const float b = std::min(row[x * 3 + 2], 65504.0f);
									luminance += 0.2126 * r + 0.7152 * g + 0.0722 * b;
								}
							}

							const double value = luminance * sinTheta;
							importance[static_cast<size_t>(cellY) * environmentTableWidth + cellX] = value;
							total += value;
						}
					}

					stbi_image_free(pixels);

					if (total <= 0.0) {
						// A black map: fall back to uniform sampling
						std::fill(importance.begin(), importance.end(), 1.0);
						total = static_cast<double>(cellCount);
					}

					// Alias table (Vose's method): sampling a cell becomes one uniform pick plus one comparison.
					std::vector<Fox::Graphics::Vulkan::EnvironmentCell> cells(cellCount);
					std::vector<double> scaled(cellCount);
					std::vector<uint32_t> small, large;
					small.reserve(cellCount);
					large.reserve(cellCount);

					for (size_t i = 0; i < cellCount; i++) {
						const double probability = importance[i] / total;
						cells[i].probability = static_cast<float>(probability);
						cells[i].alias = static_cast<uint32_t>(i);
						cells[i].threshold = 1.0f;
						cells[i].pad = 0.0f;

						scaled[i] = probability * static_cast<double>(cellCount);
						(scaled[i] < 1.0 ? small : large).push_back(static_cast<uint32_t>(i));
					}

					while (!small.empty() && !large.empty()) {
						const uint32_t less = small.back();
						const uint32_t more = large.back();
						small.pop_back();

						// The under-full cell keeps its share and hands the rest of its slot to the over-full one
						cells[less].threshold = static_cast<float>(scaled[less]);
						cells[less].alias = more;

						scaled[more] -= 1.0 - scaled[less];
						if (scaled[more] < 1.0) {
							large.pop_back();
							small.push_back(more);
						}
					}

					environmentSamplingTable = std::make_unique<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Vulkan::EnvironmentCell>>(device, physicalDevice, "Environment Sampling Table", cells);
					environmentSamplingTable->Update(cells);
				}

				void TextureManager::Resize(VkExtent2D extent) {
					depthTextures[Fox::Graphics::Managers::Vulkan::DepthTexture::DEFAULT_DEPTH_STENCIL] =
						std::make_unique<Fox::Graphics::Vulkan::DepthTexture>(device, physicalDevice, VkExtent3D{ extent.width, extent.height, 1 }, depthFormat);
				}

				void TextureManager::UpdateEnvironment() {
					Fox::Graphics::Vulkan::Environment env{};
					env.environmentMapIntensity = ENVIRONMENT_MAP_INTENSITY;
					env.environmentMapTextureIndex = ENVIRONMENT_MAP_TEXTURE_INDEX;
					env.tableWidth = environmentTableWidth;
					env.tableHeight = environmentTableHeight;

					environmentUBO->Update(env);
				}

			}
		}
	}
}
