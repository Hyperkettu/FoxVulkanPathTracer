#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				namespace {
					const VkFormat ILLUMINATION_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
					const VkFormat MOMENTS_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;

					// Push constant blocks, must match the shaders
					struct TemporalConstants {
						uint32_t resetHistory;
					};

					struct AtrousConstants {
						int32_t stepSize;      // Distance between filter taps in pixels
						uint32_t writeHistory; // 1: also store the result as next frame's lighting history
					};

					// Descriptor sets of the a-trous pass
					enum AtrousSet : uint32_t { PING_TO_PONG = 0, PONG_TO_PING = 1 };
				}

				SpatiotemporalVarianceGuidedFilter::SpatiotemporalVarianceGuidedFilter(const DeviceContext& context)
					: context(context)
					, temporalPass(context.device, "Shaders/svgf_temporal.spv", 10, 1, sizeof(TemporalConstants))
					, variancePass(context.device, "Shaders/svgf_variance.spv", 5)
					, atrousPass(context.device, "Shaders/svgf_atrous.spv", 4, 2, sizeof(AtrousConstants))
					, modulatePass(context.device, "Shaders/svgf_modulate.spv", 6)
				{
				}

				void SpatiotemporalVarianceGuidedFilter::Resize(const GBuffer& gbuffer) {
					extent = gbuffer.extent;

					illuminationPing = CreateStorageImage(context, extent, ILLUMINATION_FORMAT);
					illuminationPong = CreateStorageImage(context, extent, ILLUMINATION_FORMAT);
					illuminationHistory = CreateStorageImage(context, extent, ILLUMINATION_FORMAT);
					momentsHistory = CreateStorageImage(context, extent, MOMENTS_FORMAT);
					moments = CreateStorageImage(context, extent, MOMENTS_FORMAT);
					normalDepthHistory = CreateStorageImage(context, extent, GBuffer::NORMAL_DEPTH_FORMAT);
					output = CreateStorageImage(context, extent, GBuffer::RADIANCE_FORMAT);

					// The binding order below mirrors the declarations at the top of each shader.
					temporalPass.BindImages(0, {
						gbuffer.radiance->GetView(),
						gbuffer.albedo->GetView(),
						gbuffer.normalDepth->GetView(),
						gbuffer.motion->GetView(),
						normalDepthHistory->GetView(),
						illuminationHistory->GetView(),
						momentsHistory->GetView(),
						illuminationPong->GetView(), // out: integrated lighting
						moments->GetView(),          // out
						gbuffer.specular->GetView(),
					});

					variancePass.BindImages(0, {
						illuminationPong->GetView(),
						moments->GetView(),
						gbuffer.normalDepth->GetView(),
						illuminationPing->GetView(), // out: lighting + variance
						momentsHistory->GetView(),   // out: becomes next frame's history
					});

					atrousPass.BindImages(PING_TO_PONG, {
						illuminationPing->GetView(),
						illuminationPong->GetView(),
						gbuffer.normalDepth->GetView(),
						illuminationHistory->GetView(),
					});

					atrousPass.BindImages(PONG_TO_PING, {
						illuminationPong->GetView(),
						illuminationPing->GetView(),
						gbuffer.normalDepth->GetView(),
						illuminationHistory->GetView(),
					});

					// With an odd iteration count the filtered result ends up in the pong image
					static_assert(ATROUS_ITERATIONS % 2 == 1, "The modulate pass reads illuminationPong");

					modulatePass.BindImages(0, {
						illuminationPong->GetView(),
						gbuffer.albedo->GetView(),
						gbuffer.normalDepth->GetView(),
						normalDepthHistory->GetView(), // out: becomes next frame's history
						output->GetView(),             // out
						gbuffer.specular->GetView(),
					});
				}

				void SpatiotemporalVarianceGuidedFilter::Denoise(CommandList& commandList, const DenoiserFrameInfo& frame) {
					// Each pass reads what the previous one wrote
					auto waitForPreviousPass = [&commandList]() {
						commandList.MemoryBarrier(
							VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
							VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
					};

					const TemporalConstants temporalConstants{ frame.resetHistory ? 1u : 0u };
					temporalPass.Dispatch(commandList, extent, 0, &temporalConstants);
					waitForPreviousPass();

					variancePass.Dispatch(commandList, extent);
					waitForPreviousPass();

					for (uint32_t i = 0; i < ATROUS_ITERATIONS; i++) {
						AtrousConstants constants{};
						constants.stepSize = 1 << i;
						constants.writeHistory = (i == 0) ? 1u : 0u;

						atrousPass.Dispatch(commandList, extent, (i % 2 == 0) ? PING_TO_PONG : PONG_TO_PING, &constants);
						waitForPreviousPass();
					}

					modulatePass.Dispatch(commandList, extent);
				}

			}
		}
	}
}
