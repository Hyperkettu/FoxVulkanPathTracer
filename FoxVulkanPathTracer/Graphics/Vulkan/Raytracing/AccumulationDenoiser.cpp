#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				namespace {
					// Must match the push constant block in Shaders/accumulate.slang
					struct AccumulateConstants {
						uint32_t accumulatedFrames;
					};
				}

				AccumulationDenoiser::AccumulationDenoiser(const DeviceContext& context)
					: context(context)
					, accumulatePass(context.device, "Shaders/accumulate.spv", 2, 1, sizeof(AccumulateConstants))
				{
				}

				void AccumulationDenoiser::Resize(const GBuffer& gbuffer, const OutputSettings& /*output*/) {
					extent = gbuffer.extent;
					accumulation = CreateStorageImage(context, extent, VK_FORMAT_R32G32B32A32_SFLOAT);
					accumulatedFrames = 0;

					accumulatePass.BindImages(0, { gbuffer.radiance->GetView(), accumulation->GetView() });
				}

				void AccumulationDenoiser::Denoise(CommandList& commandList, const DenoiserFrameInfo& frame) {
					// The average is only valid for a static view
					if (frame.resetHistory || frame.cameraMoved) {
						accumulatedFrames = 0;
					}

					const AccumulateConstants constants{ accumulatedFrames };
					accumulatePass.Dispatch(commandList, extent, 0, &constants);

					accumulatedFrames++;
				}

			}
		}
	}
}
