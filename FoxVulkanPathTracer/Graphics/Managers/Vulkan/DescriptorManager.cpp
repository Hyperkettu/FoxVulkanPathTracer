#include "FoxRenderer.h"

// Static member definitions
std::unique_ptr<Fox::Graphics::Managers::Vulkan::DescriptorManager> Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::DescriptorManager>::instance = nullptr;
std::once_flag Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::DescriptorManager>::initFlag;

namespace Fox {

	namespace Graphics {

		namespace Managers {

			namespace Vulkan {

				bool DescriptorManager::Initialize(VkDevice device, const Fox::Graphics::RendererConfig& config) {
					{
						auto mainMeshPass = std::make_unique<Fox::Graphics::Vulkan::DescriptorSetBuilder>(Fox::Graphics::Vulkan::DescriptorSetBuilder(device));
						mainMeshPass
							->AddBinding(0, VkDescriptorType::VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VkShaderStageFlagBits::VK_SHADER_STAGE_RAYGEN_BIT_KHR | VkShaderStageFlagBits::VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
							.AddBinding(1, VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VkShaderStageFlagBits::VK_SHADER_STAGE_RAYGEN_BIT_KHR)
							.AddBinding(2, VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VkShaderStageFlagBits::VK_SHADER_STAGE_RAYGEN_BIT_KHR | VkShaderStageFlagBits::VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
							.SetMaxSets(config.MAX_FRAMES_IN_FLIGHT)
							.Build();

						descriptorSetBuilders[Fox::Graphics::Managers::Vulkan::Descriptor::MAIN_MESH_SHADER] = std::move(mainMeshPass);
						
					} 
					{
						auto offscreenDescriptorSets = std::make_unique<Fox::Graphics::Vulkan::DescriptorSetBuilder>(Fox::Graphics::Vulkan::DescriptorSetBuilder(device));
						offscreenDescriptorSets
							->AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VkShaderStageFlagBits::VK_SHADER_STAGE_MESH_BIT_EXT)
							.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT)
							.SetMaxSets(config.MAX_FRAMES_IN_FLIGHT)
							.Build();

						descriptorSetBuilders[Fox::Graphics::Managers::Vulkan::Descriptor::OFFSCREEN] = std::move(offscreenDescriptorSets);
					}

					return true;
				}

			}
		}
	}
}