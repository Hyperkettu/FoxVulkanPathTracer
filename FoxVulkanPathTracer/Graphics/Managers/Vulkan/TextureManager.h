#pragma once

#include <unordered_map>

namespace Fox {

	namespace Graphics {

		namespace Managers {

			namespace Vulkan {

				enum class DepthTexture : int32_t {
					DEFAULT_DEPTH_STENCIL = 0,
				};

				enum class ShaderResourceTexture : int32_t {
					BOX = 0,
				};

				enum class RenderTargetTexture : int32_t {
					 DEFAULT_RENDER_TARGET = 0,
				};

				class TextureManager : public Fox::Core::Singleton<TextureManager> {
					friend class Singleton<Fox::Graphics::Managers::Vulkan::TextureManager>;

				public: 
					TextureManager() = default;
					~TextureManager() = default;
					
					bool Initialize(
						VkDevice device,
						VkPhysicalDevice physicalDevice,
						VkSurfaceCapabilitiesKHR capabilities,
						VkSurfaceFormatKHR surfaceFormat,
						VkFormat depthFormat,
						VkQueue graphicsQueue,
						std::unique_ptr<Fox::Graphics::Vulkan::CommandPool>& commandPool);

					void Destroy() {
						depthTextures.clear();
						shaderResourceTextures.clear();

						//for (auto& texture : shaderResourceTextures) {
						// delete texture;
						//}

						renderTargetTextures.clear();

						for (auto* texture : bindlessTextureArray) {
							SAFE_DELETE(texture); 
						}
					}

					inline uint32_t AddBindlessTexture(Fox::Graphics::Vulkan::Texture* texture) {

						bindlessTextureArray.push_back(texture);
						return bindlessTextureArray.size() - 1;
					}

					inline std::vector<Fox::Graphics::Vulkan::Texture*>& GetBindlessTextureArray() {
						return bindlessTextureArray; 
					}

					inline std::unique_ptr<Fox::Graphics::Vulkan::DepthTexture>& GetDepthTexture(Fox::Graphics::Managers::Vulkan::DepthTexture depthTexture) {
						return depthTextures[depthTexture];
					}

					inline std::unique_ptr<Fox::Graphics::Vulkan::ShaderResourceTexture>& GetShaderResourceTexture(Fox::Graphics::Managers::Vulkan::ShaderResourceTexture shaderResourceTexture) {
						return shaderResourceTextures[shaderResourceTexture];
					}

					inline std::unique_ptr<Fox::Graphics::Vulkan::RenderTargetTexture>& GetRenderTargetTexture(Fox::Graphics::Managers::Vulkan::RenderTargetTexture renderTargetTexture) {
						return renderTargetTextures[renderTargetTexture];
					}

					std::unique_ptr<Fox::Graphics::Vulkan::DescriptorSet > bindlessTextureDescriptorSet;  

				private:
					std::unordered_map<Fox::Graphics::Managers::Vulkan::DepthTexture, std::unique_ptr<Fox::Graphics::Vulkan::DepthTexture>> depthTextures;
					std::unordered_map<Fox::Graphics::Managers::Vulkan::ShaderResourceTexture, std::unique_ptr<Fox::Graphics::Vulkan::ShaderResourceTexture>> shaderResourceTextures;
					std::unordered_map<Fox::Graphics::Managers::Vulkan::RenderTargetTexture, std::unique_ptr<Fox::Graphics::Vulkan::RenderTargetTexture>> renderTargetTextures;

					std::vector<Fox::Graphics::Vulkan::Texture*> bindlessTextureArray;
				};
			}
		}
	}
}