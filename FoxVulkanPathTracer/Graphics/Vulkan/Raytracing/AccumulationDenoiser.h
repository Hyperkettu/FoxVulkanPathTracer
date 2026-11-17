#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				// Reference "denoiser": averages every frame rendered since the camera last moved.
				// Converges to the ground truth for a static view and shows raw noise while moving.
				class AccumulationDenoiser : public Denoiser {
				public:
					explicit AccumulationDenoiser(const DeviceContext& context);

					const char* GetName() const override { return "Accumulation (reference)"; }

					void Resize(const GBuffer& gbuffer, const OutputSettings& output) override;
					void Denoise(CommandList& commandList, const DenoiserFrameInfo& frame) override;

					VkImageView GetOutputView() const override { return accumulation->GetView(); }

					// Random sub-pixel offsets average out to an anti-aliased image
					JitterMode GetJitterMode() const override { return JitterMode::Random; }

				private:
					DeviceContext context;
					VkExtent2D extent{};

					ComputePass accumulatePass;

					// 32-bit floats: a running average over thousands of frames needs the precision
					std::unique_ptr<StorageTexture> accumulation;
					uint32_t accumulatedFrames = 0;
				};

			}
		}
	}
}
