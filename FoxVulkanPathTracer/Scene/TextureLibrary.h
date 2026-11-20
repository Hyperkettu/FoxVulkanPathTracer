#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Fox {

	namespace Scene {

		// A single channel image in [0, 1], repeating in both directions
		struct GrayImage {
			int width = 0;
			int height = 0;
			std::vector<float> values;

			float At(int x, int y) const;

			// Bilinear, at texture coordinates (texel centers at (i + 0.5) / size, like the GPU)
			float Sample(float u, float v) const;

			// Averages factor x factor blocks
			GrayImage Downsampled(int factor) const;
		};

		// 8 and 16 bit images, converted to one channel (grey) at full precision. Throws if it cannot be read.
		GrayImage LoadGray(const std::string& path);

		// An 8 bit RGBA picture in memory (straight alpha), for drawing on the CPU (world UI canvases)
		struct RgbaImage {
			int width = 0;
			int height = 0;
			std::vector<uint8_t> pixels;
		};

		// Loads textures for materials made at runtime (the tree editor), each file once. Everything it loads
		// stays loaded, so materials can share textures freely; GetVersion changes whenever a texture is added
		// (the renderer must then rewrite its texture descriptors).
		class TextureLibrary {
		public:
			TextureLibrary(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue)
				: device(device), physicalDevice(physicalDevice), commandPool(commandPool), queue(queue) {}

			// Bindless texture index of an image file; -1 for an empty name
			int Texture(const std::string& file, bool srgb);

			// A new texture from 8 bit RGBA pixels (linear), e.g. one that is then edited (a terrain's blend map)
			int CreateTexture(const std::vector<uint8_t>& rgba, int width, int height, bool srgb = false);

			// A fill image (its colour; empty: white) cut out by a mask image (its alpha; empty: the fill's own), both
			// stretched over the same 0..1 coordinates: decorated, textured text and sprites. srgb: for the path tracer
			// (colour decoded); not for the screen-space UI (which draws the stored values). -1 if neither is given.
			int MaskedTexture(const std::string& fill, const std::string& mask, bool srgb);
			// The same picture in memory (loaded once); throws if a file cannot be read
			const RgbaImage& MaskedPixels(const std::string& fill, const std::string& mask);

			// Occlusion (R), roughness (G) and metallic (B) packed into one texture, the glTF way. Missing files
			// count as no occlusion (1), roughness 1 and metallic 0. Files of other sizes are resampled.
			int PackedOcclusionRoughnessMetallic(const std::string& occlusion, const std::string& roughness, const std::string& metallic);

			// The alpha channel of an image, for opacity micromaps; nullptr for an empty name
			const Fox::Graphics::Vulkan::RayTracing::AlphaMask* Alpha(const std::string& file);

			// A height map for displacement, averaged down by factor. invert: 1 - value. mask: outside where the
			// mask image's alpha is below one half, the height is midlevel. nullptr for an empty name.
			std::shared_ptr<const GrayImage> Height(const std::string& file, int factor, bool invert, const std::string& mask, float midlevel);

			uint32_t GetVersion() const { return version; }

			// Image files under folder (recursively), with forward slashes, sorted
			static std::vector<std::string> ListImages(const std::string& folder = "Textures");

		private:
			VkDevice device;
			VkPhysicalDevice physicalDevice;
			VkCommandPool commandPool;
			VkQueue queue;
			uint32_t version = 0;

			std::map<std::string, int> textures;
			std::map<std::string, int> packed;
			std::map<std::string, int> masked;
			std::map<std::string, RgbaImage> maskedPixels;
			std::map<std::string, std::unique_ptr<Fox::Graphics::Vulkan::RayTracing::AlphaMask>> alphas;
			std::map<std::string, std::shared_ptr<const GrayImage>> heights;
			std::map<std::string, GrayImage> grays; // Loaded files, by path
			const GrayImage& Gray(const std::string& file);
			void ComposeMasked(const std::string& fill, const std::string& mask, RgbaImage& image);
		};
	}
}
