#include "FoxRenderer.h"

#include <filesystem>

namespace Fox {

	namespace Scene {

		float GrayImage::At(int x, int y) const {
			x = ((x % width) + width) % width;
			y = ((y % height) + height) % height;
			return values[static_cast<size_t>(y) * width + x];
		}

		float GrayImage::Sample(float u, float v) const {
			if (width == 0 || height == 0) {
				return 0.0f;
			}
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

		GrayImage GrayImage::Downsampled(int factor) const {
			factor = std::max(factor, 1);
			GrayImage result;
			result.width = std::max(width / factor, 1);
			result.height = std::max(height / factor, 1);
			result.values.assign(static_cast<size_t>(result.width) * result.height, 0.0f);
			std::vector<int> counts(result.values.size(), 0);
			for (int y = 0; y < height; y++) {
				for (int x = 0; x < width; x++) {
					const size_t target = static_cast<size_t>(std::min(y / factor, result.height - 1)) * result.width + std::min(x / factor, result.width - 1);
					result.values[target] += values[static_cast<size_t>(y) * width + x];
					counts[target]++;
				}
			}
			for (size_t i = 0; i < result.values.size(); i++) {
				result.values[i] /= static_cast<float>(std::max(counts[i], 1));
			}
			return result;
		}

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

		int TextureLibrary::Texture(const std::string& file, bool srgb) {
			if (file.empty()) {
				return -1;
			}
			const std::string key = file + (srgb ? "|srgb" : "|linear");
			if (auto it = textures.find(key); it != textures.end()) {
				return it->second;
			}

			auto texture = Fox::Graphics::Vulkan::ShaderResourceTexture::LoadFromFile(device, physicalDevice, commandPool, queue, file, srgb);
			const int index = static_cast<int>(Fox::Graphics::Managers::Vulkan::TextureManager::Get().AddBindlessTexture(std::move(texture)));
			textures[key] = index;
			version++;
			return index;
		}

		const GrayImage& TextureLibrary::Gray(const std::string& file) {
			if (auto it = grays.find(file); it != grays.end()) {
				return it->second;
			}
			return grays.emplace(file, LoadGray(file)).first->second;
		}

		int TextureLibrary::PackedOcclusionRoughnessMetallic(const std::string& occlusion, const std::string& roughness, const std::string& metallic) {
			const std::string key = occlusion + "|" + roughness + "|" + metallic;
			if (auto it = packed.find(key); it != packed.end()) {
				return it->second;
			}

			const GrayImage* channels[3] = {
				occlusion.empty() ? nullptr : &Gray(occlusion),
				roughness.empty() ? nullptr : &Gray(roughness),
				metallic.empty() ? nullptr : &Gray(metallic),
			};
			const float defaults[3] = { 1.0f, 1.0f, 0.0f };

			// The size of the first file given; the others are sampled to it
			int width = 1, height = 1;
			for (const GrayImage* channel : channels) {
				if (channel) {
					width = channel->width;
					height = channel->height;
					break;
				}
			}

			std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 255);
			for (int y = 0; y < height; y++) {
				for (int x = 0; x < width; x++) {
					const size_t i = (static_cast<size_t>(y) * width + x) * 4;
					for (int c = 0; c < 3; c++) {
						float value = defaults[c];
						if (const GrayImage* channel = channels[c]) {
							value = channel->width == width && channel->height == height
								? channel->values[static_cast<size_t>(y) * width + x]
								: channel->Sample((x + 0.5f) / width, (y + 0.5f) / height);
						}
						pixels[i + c] = static_cast<uint8_t>(std::lround(glm::clamp(value, 0.0f, 1.0f) * 255.0f));
					}
				}
			}

			auto texture = Fox::Graphics::Vulkan::ShaderResourceTexture::LoadFromPixelData(device, physicalDevice, commandPool, queue,
				pixels.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height), VK_FORMAT_R8G8B8A8_UNORM, 4);
			const int index = static_cast<int>(Fox::Graphics::Managers::Vulkan::TextureManager::Get().AddBindlessTexture(std::move(texture)));
			packed[key] = index;
			version++;
			return index;
		}

		const Fox::Graphics::Vulkan::RayTracing::AlphaMask* TextureLibrary::Alpha(const std::string& file) {
			if (file.empty()) {
				return nullptr;
			}
			if (auto it = alphas.find(file); it != alphas.end()) {
				return it->second.get();
			}

			int width = 0, height = 0, channels = 0;
			stbi_uc* rgba = stbi_load(file.c_str(), &width, &height, &channels, 4);
			if (!rgba) {
				throw std::runtime_error("Failed to load " + file);
			}
			auto mask = std::make_unique<Fox::Graphics::Vulkan::RayTracing::AlphaMask>();
			mask->width = static_cast<uint32_t>(width);
			mask->height = static_cast<uint32_t>(height);
			mask->alpha.resize(static_cast<size_t>(width) * height);
			for (size_t i = 0; i < mask->alpha.size(); i++) {
				mask->alpha[i] = rgba[i * 4 + 3];
			}
			stbi_image_free(rgba);
			return (alphas[file] = std::move(mask)).get();
		}

		std::shared_ptr<const GrayImage> TextureLibrary::Height(const std::string& file, int factor, bool invert, const std::string& mask, float midlevel) {
			if (file.empty()) {
				return nullptr;
			}
			const std::string key = file + "|" + std::to_string(factor) + "|" + (invert ? "1" : "0") + "|" + mask + "|" + std::to_string(midlevel);
			if (auto it = heights.find(key); it != heights.end()) {
				return it->second;
			}

			GrayImage image = Gray(file);
			const Fox::Graphics::Vulkan::RayTracing::AlphaMask* alpha = Alpha(mask);
			for (int y = 0; y < image.height; y++) {
				for (int x = 0; x < image.width; x++) {
					float& value = image.values[static_cast<size_t>(y) * image.width + x];
					if (invert) {
						value = 1.0f - value;
					}
					if (alpha && alpha->Sample((x + 0.5f) / image.width, (y + 0.5f) / image.height) < 0.5f) {
						value = midlevel;
					}
				}
			}

			auto result = std::make_shared<const GrayImage>(image.Downsampled(factor));
			heights[key] = result;
			return result;
		}

		std::vector<std::string> TextureLibrary::ListImages(const std::string& folder) {
			std::vector<std::string> files;
			std::error_code error;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, error)) {
				if (!entry.is_regular_file()) {
					continue;
				}
				std::string extension = entry.path().extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp") {
					files.push_back(entry.path().generic_string());
				}
			}
			std::sort(files.begin(), files.end());
			return files;
		}
	}
}
