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
						auto raytracingDescriptorSets = std::make_unique<Fox::Graphics::Vulkan::DescriptorSetBuilder>(Fox::Graphics::Vulkan::DescriptorSetBuilder(device));
						raytracingDescriptorSets
							->AddBinding(0, VkDescriptorType::VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VkShaderStageFlagBits::VK_SHADER_STAGE_RAYGEN_BIT_KHR | VkShaderStageFlagBits::VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
							.AddBinding(1, VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VkShaderStageFlagBits::VK_SHADER_STAGE_RAYGEN_BIT_KHR)
							.AddBinding(2, VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VkShaderStageFlagBits::VK_SHADER_STAGE_RAYGEN_BIT_KHR | VkShaderStageFlagBits::VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
							.AddBinding(3, VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VkShaderStageFlagBits::VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) //, VkDescriptorBindingFlagBits::VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, 100)
							.AddBinding(4, VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VkShaderStageFlagBits::VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) //, VkDescriptorBindingFlagBits::VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, 100)
							.AddBinding(5, VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VkShaderStageFlagBits::VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) //, VkDescriptorBindingFlagBits::VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VkDescriptorBindingFlagBits::VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 100)
							.AddBinding(6, VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VkShaderStageFlagBits::VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) 
							.SetMaxSets(config.MAX_FRAMES_IN_FLIGHT)
							.Build();

						descriptorSetBuilders[Fox::Graphics::Managers::Vulkan::Descriptor::RAY_TRACING] = std::move(raytracingDescriptorSets);
						
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