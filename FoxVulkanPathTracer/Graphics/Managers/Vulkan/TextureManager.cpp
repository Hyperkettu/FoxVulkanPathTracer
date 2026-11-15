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

					ENVIRONMENT_MAP_TEXTURE_INDEX = AddBindlessTexture(
						Fox::Graphics::Vulkan::ShaderResourceTexture::LoadFromFile(device, physicalDevice, commandPool, graphicsQueue, "Textures/environment_map.hdr"));

					return true;
				}

				void TextureManager::Resize(VkExtent2D extent) {
					depthTextures[Fox::Graphics::Managers::Vulkan::DepthTexture::DEFAULT_DEPTH_STENCIL] =
						std::make_unique<Fox::Graphics::Vulkan::DepthTexture>(device, physicalDevice, VkExtent3D{ extent.width, extent.height, 1 }, depthFormat);
				}

				void TextureManager::UpdateEnvironment() {
					Fox::Graphics::Vulkan::Environment env{};
					env.environmentMapIntensity = ENVIRONMENT_MAP_INTENSITY;
					env.environmentMapTextureIndex = ENVIRONMENT_MAP_TEXTURE_INDEX;

					environmentUBO->Update(env);
				}

			}
		}
	}
}
