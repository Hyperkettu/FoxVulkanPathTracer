#pragma once

#include <unordered_map>

namespace Fox {

	namespace Graphics {

		namespace Managers {

			namespace Vulkan {

				enum class PipelineCategory : int32_t {
					BASIC_MESH_SHADER = 0,
					OFFSCREEN_RENDERING = 1,
					POST_PROCESSOR = 2 
				};

				class PipelineManager : public Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::PipelineManager> {
					friend class Singleton<Fox::Graphics::Managers::Vulkan::PipelineManager>;

				public: 
					PipelineManager() = default;
					~PipelineManager() = default;


					bool Initialize(VkDevice device, VkSurfaceCapabilitiesKHR capabilities);

					void Destroy() {
						pipelines.clear();
						pipelineLayouts.clear();
					}

					inline std::unique_ptr<Fox::Graphics::Vulkan::Pipeline>& GetPipeline(Fox::Graphics::Managers::Vulkan::PipelineCategory category) {
						return pipelines[category];
					}
					inline std::unique_ptr<Fox::Graphics::Vulkan::PipelineLayout>& GetPipelineLayout(Fox::Graphics::Managers::Vulkan::PipelineCategory category) {
						return pipelineLayouts[category];
					}


				private:
					std::unordered_map<Fox::Graphics::Managers::Vulkan::PipelineCategory, std::unique_ptr<Fox::Graphics::Vulkan::Pipeline>> pipelines;
					std::unordered_map<Fox::Graphics::Managers::Vulkan::PipelineCategory, std::unique_ptr<Fox::Graphics::Vulkan::PipelineLayout>> pipelineLayouts;

				};
			}
		}
	}
}