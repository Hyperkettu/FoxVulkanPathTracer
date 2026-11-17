#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				std::vector<std::unique_ptr<Denoiser>> CreateDenoisers(const DeviceContext& context) {
					std::vector<std::unique_ptr<Denoiser>> denoisers;

					// Register new denoisers here. The first entry is the one active at startup.

#ifdef FOX_ENABLE_DLSS
					// Optional: needs an NVIDIA RTX GPU, a recent driver and nvngx_dlssd.dll next to the executable
					try {
						denoisers.push_back(std::make_unique<DlssRayReconstruction>(context));
					}
					catch (const std::exception& e) {
						std::cout << "DLSS Ray Reconstruction is not available: " << e.what() << "\n";
					}
#endif

					denoisers.push_back(std::make_unique<SpatiotemporalVarianceGuidedFilter>(context));
					denoisers.push_back(std::make_unique<AccumulationDenoiser>(context));

					return denoisers;
				}

				float RenderScale(UpscalingMode mode) {
					// The scale factors DLSS uses for its modes
					switch (mode) {
					case UpscalingMode::Quality: return 2.0f / 3.0f;
					case UpscalingMode::Balanced: return 0.58f;
					case UpscalingMode::Performance: return 0.5f;
					case UpscalingMode::UltraPerformance: return 1.0f / 3.0f;
					default: return 1.0f;
					}
				}

				const char* UpscalingModeName(UpscalingMode mode) {
					switch (mode) {
					case UpscalingMode::Quality: return "Quality";
					case UpscalingMode::Balanced: return "Balanced";
					case UpscalingMode::Performance: return "Performance";
					case UpscalingMode::UltraPerformance: return "Ultra Performance";
					default: return "Native";
					}
				}

				VkExtent2D RenderExtentFor(VkExtent2D displayExtent, UpscalingMode mode) {
					const float scale = RenderScale(mode);
					return VkExtent2D{
						std::max(1u, static_cast<uint32_t>(std::lround(displayExtent.width * scale))),
						std::max(1u, static_cast<uint32_t>(std::lround(displayExtent.height * scale)))
					};
				}

				std::vector<std::string> GetDenoiserInstanceExtensions() {
#ifdef FOX_ENABLE_DLSS
					return DlssRayReconstruction::GetInstanceExtensions();
#else
					return {};
#endif
				}

				std::vector<std::string> GetDenoiserDeviceExtensions(VkInstance instance, VkPhysicalDevice physicalDevice) {
#ifdef FOX_ENABLE_DLSS
					return DlssRayReconstruction::GetDeviceExtensions(instance, physicalDevice);
#else
					return {};
#endif
				}

			}
		}
	}
}
