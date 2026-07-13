#pragma once

#include <unordered_map>

namespace Fox {

	namespace Graphics {

		namespace Managers {

			namespace Vulkan {

				enum class Descriptor : int32_t {
					OFFSCREEN = 0,
					BINDLESS_TEXTURES,
					RAY_TRACING
				};

				class DescriptorManager : public Fox::Core::Singleton<DescriptorManager> {
					friend class Singleton <Fox::Graphics::Managers::Vulkan::DescriptorManager>;

				public: 

					DescriptorManager() = default;
					~DescriptorManager() = default;

					bool Initialize(VkDevice device, const Fox::Graphics::RendererConfig& config);

					void Destroy() {
						descriptorSetBuilders.clear();
					}

					std::unique_ptr<Fox::Graphics::Vulkan::DescriptorSetBuilder>& GetDescriptorSet(Fox::Graphics::Managers::Vulkan::Descriptor set) {
						return descriptorSetBuilders[set];
					}

				private:
					std::unordered_map<Fox::Graphics::Managers::Vulkan::Descriptor, std::unique_ptr<Fox::Graphics::Vulkan::DescriptorSetBuilder>> descriptorSetBuilders;
				};
			}
		}
	}
}