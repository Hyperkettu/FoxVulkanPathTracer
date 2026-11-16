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
