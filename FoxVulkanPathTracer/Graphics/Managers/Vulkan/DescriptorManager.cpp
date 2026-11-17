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
						// Set 0 of the ray tracing pipeline. The binding numbers are shared with the shaders
						// (raygen.slang, lighting.slang, closest_hit.slang).
						auto raytracingDescriptorSets = std::make_unique<Fox::Graphics::Vulkan::DescriptorSetBuilder>(device);
						raytracingDescriptorSets
							->AddBinding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
							.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)   // G-buffer: radiance
							.AddBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR)  // Camera
							.AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)  // Vertices
							.AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)  // Indices
							.AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR)  // Submeshes (raygen: shadow rays through glass)
							.AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR)  // Materials
							.AddBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR)  // Lights
							.AddBinding(8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)   // G-buffer: diffuse albedo
							.AddBinding(9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)   // G-buffer: normal + depth
							.AddBinding(10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)  // G-buffer: motion
							.AddBinding(11, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)  // G-buffer: specular albedo + roughness
							.AddBinding(12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR) // Environment importance sampling table
							.SetMaxSets(config.MAX_FRAMES_IN_FLIGHT)
							.Build();

						descriptorSetBuilders[Fox::Graphics::Managers::Vulkan::Descriptor::RAY_TRACING] = std::move(raytracingDescriptorSets);
						
					} 
					{
						// Set 1: environment settings + every texture used by the scene
						auto bindlessTextureArrayDescriptorSet = std::make_unique<Fox::Graphics::Vulkan::DescriptorSetBuilder>(device);
						bindlessTextureArrayDescriptorSet
							->AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
							.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
								VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT,
								Fox::Graphics::Managers::Vulkan::TextureManager::MAX_BINDLESS_TEXTURES)
							.SetMaxSets(1) 
							.Build();

						descriptorSetBuilders[Fox::Graphics::Managers::Vulkan::Descriptor::BINDLESS_TEXTURES] = std::move(bindlessTextureArrayDescriptorSet);
					}
					{
						// Tone mapping pass: binding 0 = the HDR image produced by the path tracer.
						auto toneMappingDescriptorSets = std::make_unique<Fox::Graphics::Vulkan::DescriptorSetBuilder>(device);
						toneMappingDescriptorSets
							->AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT)
							.SetMaxSets(1)
							.Build();

						descriptorSetBuilders[Fox::Graphics::Managers::Vulkan::Descriptor::TONE_MAPPING] = std::move(toneMappingDescriptorSets);
					}

					return true;
				}

			}
		}
	}
}