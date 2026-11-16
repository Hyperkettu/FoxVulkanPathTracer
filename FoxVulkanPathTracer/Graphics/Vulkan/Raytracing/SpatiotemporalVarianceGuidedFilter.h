#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				// Spatiotemporal Variance-Guided Filtering (Schied et al. 2017).
				//
				// Works on lighting with the surface albedo divided out, so texture detail is never blurred:
				//   1. Temporal:  reproject last frame's lighting and luminance moments, blend in the new frame
				//   2. Variance:  estimate per-pixel variance (spatially where there is little history)
				//   3. A-trous:   edge-aware wavelet filter, 5 iterations with growing step size,
				//                 steered by depth, normals and the variance estimate
				//   4. Modulate:  multiply the albedo back in
				//
				// The passes are compute shaders (Shaders/svgf_*.slang).
				class SpatiotemporalVarianceGuidedFilter : public Denoiser {
				public:
					explicit SpatiotemporalVarianceGuidedFilter(const DeviceContext& context);

					const char* GetName() const override { return "SVGF"; }

					void Resize(const GBuffer& gbuffer) override;
					void Denoise(CommandList& commandList, const DenoiserFrameInfo& frame) override;

					VkImageView GetOutputView() const override { return output->GetView(); }

					// One surface per pixel is all this filter knows: let it see through mirrors
					bool WantsReflectedSurfaces() const override { return true; }

				private:
					static constexpr uint32_t ATROUS_ITERATIONS = 5;

					DeviceContext context;
					VkExtent2D extent{};

					ComputePass temporalPass;
					ComputePass variancePass;
					ComputePass atrousPass;   // Two descriptor sets: ping -> pong and pong -> ping
					ComputePass modulatePass;

					// rgb: demodulated lighting, a: variance of its luminance
					std::unique_ptr<StorageTexture> illuminationPing;
					std::unique_ptr<StorageTexture> illuminationPong;

					// --- State carried over to the next frame ---
					std::unique_ptr<StorageTexture> illuminationHistory; // Output of the first a-trous iteration
					std::unique_ptr<StorageTexture> momentsHistory;      // r: mean luminance, g: mean squared luminance, b: history length
					std::unique_ptr<StorageTexture> normalDepthHistory;  // Last frame's G-buffer normal/depth

					std::unique_ptr<StorageTexture> moments; // This frame's moments, before they become history
					std::unique_ptr<StorageTexture> output;  // Final denoised radiance
				};

			}
		}
	}
}
