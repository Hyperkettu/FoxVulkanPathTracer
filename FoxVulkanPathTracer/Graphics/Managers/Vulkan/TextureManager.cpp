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
					VkSurfaceCapabilitiesKHR capabilities, 
					VkSurfaceFormatKHR surfaceFormat, 
					VkFormat depthFormat, 
					VkQueue graphicsQueue, 
					std::unique_ptr<Fox::Graphics::Vulkan::CommandPool>& commandPool
				) {

					environmentUBO = std::make_unique<Fox::Graphics::Vulkan::ConstantBuffer<Fox::Graphics::Vulkan::Environment>>(device, physicalDevice, "Environment UBO"); 

					
					{
						VkExtent3D extent3D = {
							capabilities.currentExtent.width,
							capabilities.currentExtent.height,
							1
						};

						auto defaultDepthStencilTexture = std::make_unique<Fox::Graphics::Vulkan::DepthTexture>(device, physicalDevice, extent3D, depthFormat);

						depthTextures[Fox::Graphics::Managers::Vulkan::DepthTexture::DEFAULT_DEPTH_STENCIL] = std::move(defaultDepthStencilTexture);
					}
		
					{
						auto texture = Fox::Graphics::Vulkan::ShaderResourceTexture::LoadFromFile(device,
							physicalDevice,
							commandPool->Get(),
							graphicsQueue,
							"Textures/box.png"
						);

						shaderResourceTextures[Fox::Graphics::Managers::Vulkan::ShaderResourceTexture::BOX] = std::make_unique<Fox::Graphics::Vulkan::ShaderResourceTexture>(std::move(*texture));
					}

					{
						auto texture = Fox::Graphics::Vulkan::ShaderResourceTexture::LoadFromFile(device,
							physicalDevice,
							commandPool->Get(),
							graphicsQueue,
							"Textures/environment_map.hdr"
						);

						ENVIRONMENT_MAP_TEXTURE_INDEX = AddBindlessTexture(texture);

					}

					{
						auto renderTargetTexture = std::make_unique<Fox::Graphics::Vulkan::RenderTargetTexture>(
							device,
							physicalDevice,
							VkExtent3D{ capabilities.currentExtent.width, capabilities.currentExtent.height, 1 },
							surfaceFormat.format,
							VK_IMAGE_ASPECT_COLOR_BIT);

						renderTargetTextures[Fox::Graphics::Managers::Vulkan::RenderTargetTexture::DEFAULT_RENDER_TARGET] = std::move(renderTargetTexture);
					}


						return true;
					}

				}

			}
		}
	}