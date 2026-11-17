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

				enum class RayTracingPipelineCategory : int32_t {
					MAIN_RAYTRACING_PIPELINE = 0
				};

				// The path tracer is iterative: every ray is traced from the ray generation shader,
				// hit and miss shaders never trace rays themselves.
				inline constexpr uint32_t MAX_RAY_RECURSION_DEPTH = 1;

				class PipelineManager : public Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::PipelineManager> {
					friend class Singleton<Fox::Graphics::Managers::Vulkan::PipelineManager>;

				public: 
					PipelineManager() = default;
					~PipelineManager() = default;


					// shaderExecutionReordering: the device has VK_NV_ray_tracing_invocation_reorder enabled,
					// so the path tracer can use the raygen variant that reorders its bounce rays
					bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceCapabilitiesKHR capabilities, bool shaderExecutionReordering);

					void Destroy() {
						pipelines.clear();
						pipelineLayouts.clear();

						rayTracingPipelines.clear();
						rayTracingPipelineLayouts.clear();
					}

					inline std::unique_ptr<Fox::Graphics::Vulkan::Pipeline>& GetPipeline(Fox::Graphics::Managers::Vulkan::PipelineCategory category) {
						return pipelines[category];
					}

					inline std::unique_ptr<Fox::Graphics::Vulkan::RayTracing::RayTracingPipeline>& GetRayTracingPipeline(Fox::Graphics::Managers::Vulkan::RayTracingPipelineCategory category) {
						return rayTracingPipelines[category];
					}

					inline std::unique_ptr<Fox::Graphics::Vulkan::PipelineLayout>& GetRayTracingPipelineLayout(Fox::Graphics::Managers::Vulkan::RayTracingPipelineCategory category) {
						return rayTracingPipelineLayouts[category];
					}

					inline std::unique_ptr<Fox::Graphics::Vulkan::PipelineLayout>& GetPipelineLayout(Fox::Graphics::Managers::Vulkan::PipelineCategory category) {
						return pipelineLayouts[category];
					}


				private:
					std::unordered_map<Fox::Graphics::Managers::Vulkan::PipelineCategory, std::unique_ptr<Fox::Graphics::Vulkan::Pipeline>> pipelines;
					std::unordered_map<Fox::Graphics::Managers::Vulkan::PipelineCategory, std::unique_ptr<Fox::Graphics::Vulkan::PipelineLayout>> pipelineLayouts;

					std::unordered_map<Fox::Graphics::Managers::Vulkan::RayTracingPipelineCategory, std::unique_ptr<Fox::Graphics::Vulkan::RayTracing::RayTracingPipeline>> rayTracingPipelines;	
					std::unordered_map<Fox::Graphics::Managers::Vulkan::RayTracingPipelineCategory, std::unique_ptr<Fox::Graphics::Vulkan::PipelineLayout>> rayTracingPipelineLayouts;

				};
			}
		}
	}
}