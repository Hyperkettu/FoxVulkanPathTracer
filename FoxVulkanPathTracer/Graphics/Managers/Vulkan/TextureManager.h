#pragma once

#include <unordered_map>

namespace Fox {

	namespace Graphics {

		namespace Managers {

			namespace Vulkan {

				enum class DepthTexture : int32_t {
					DEFAULT_DEPTH_STENCIL = 0,
				};

				// Owns the textures shared across the renderer: the depth buffer of the presentation pass
				// and the bindless array sampled by the ray tracing shaders (material textures + environment map).
				class TextureManager : public Fox::Core::Singleton<TextureManager> {
					friend class Singleton<Fox::Graphics::Managers::Vulkan::TextureManager>;

				public:
					// Size of the bindless texture array declared in the descriptor set layout.
					static constexpr uint32_t MAX_BINDLESS_TEXTURES = 100;

					TextureManager() = default;
					~TextureManager() = default;

					bool Initialize(
						VkDevice device,
						VkPhysicalDevice physicalDevice,
						VkExtent2D extent,
						VkFormat depthFormat,
						VkQueue graphicsQueue,
						VkCommandPool commandPool);

					// Recreates the resolution dependent textures. The device must be idle.
					void Resize(VkExtent2D extent);

					void Destroy() {
						bindlessTextureDescriptorSet = nullptr;
						depthTextures.clear();
						bindlessTextureArray.clear();
						environmentUBO = nullptr;
					}

					// Takes ownership and returns the index the shaders use to address the texture.
					uint32_t AddBindlessTexture(std::unique_ptr<Fox::Graphics::Vulkan::Texture> texture) {
						if (bindlessTextureArray.size() >= MAX_BINDLESS_TEXTURES) {
							throw std::runtime_error("Bindless texture array is full - raise TextureManager::MAX_BINDLESS_TEXTURES.");
						}

						bindlessTextureArray.push_back(std::move(texture));
						return static_cast<uint32_t>(bindlessTextureArray.size() - 1);
					}

					// Uploads the current environment settings (map index + intensity) to the GPU.
					void UpdateEnvironment();

					inline const std::vector<std::unique_ptr<Fox::Graphics::Vulkan::Texture>>& GetBindlessTextureArray() const {
						return bindlessTextureArray;
					}

					inline std::unique_ptr<Fox::Graphics::Vulkan::DepthTexture>& GetDepthTexture(Fox::Graphics::Managers::Vulkan::DepthTexture depthTexture) {
						return depthTextures[depthTexture];
					}

					std::unique_ptr<Fox::Graphics::Vulkan::DescriptorSet > bindlessTextureDescriptorSet;
					std::unique_ptr<Fox::Graphics::Vulkan::ConstantBuffer<Fox::Graphics::Vulkan::Environment>> environmentUBO;

					static uint32_t ENVIRONMENT_MAP_TEXTURE_INDEX;
					static float ENVIRONMENT_MAP_INTENSITY;

				private:
					VkDevice device = VK_NULL_HANDLE;
					VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
					VkFormat depthFormat = VK_FORMAT_UNDEFINED;

					std::unordered_map<Fox::Graphics::Managers::Vulkan::DepthTexture, std::unique_ptr<Fox::Graphics::Vulkan::DepthTexture>> depthTextures;

					std::vector<std::unique_ptr<Fox::Graphics::Vulkan::Texture>> bindlessTextureArray;

				};
			}
		}
	}
}
