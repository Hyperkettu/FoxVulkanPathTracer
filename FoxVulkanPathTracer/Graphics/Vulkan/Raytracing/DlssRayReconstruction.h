#pragma once

#ifdef FOX_ENABLE_DLSS

// From the NGX SDK; kept out of this header so only DlssRayReconstruction.cpp depends on it
struct NVSDK_NGX_Handle;
struct NVSDK_NGX_Parameter;

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				// NVIDIA DLSS Ray Reconstruction (DLSS-RR): a neural network that denoises and anti-aliases
				// the path traced image in one step, guided by the G-buffer. Runs in DLAA mode (output
				// resolution = render resolution) with the newest transformer model (render preset F).
				//
				// Only compiled when the DLSS SDK is present in ThirdParty/DLSS (see the readme). At runtime it
				// needs an RTX GPU, a current driver and nvngx_dlssd.dll next to the executable; if any of that
				// is missing the constructor throws and the renderer carries on without it.
				class DlssRayReconstruction : public Denoiser {
				public:
					explicit DlssRayReconstruction(const DeviceContext& context);
					~DlssRayReconstruction() override;

					DlssRayReconstruction(const DlssRayReconstruction&) = delete;
					DlssRayReconstruction& operator=(const DlssRayReconstruction&) = delete;

					const char* GetName() const override { return "DLSS Ray Reconstruction"; }

					void Resize(const GBuffer& gbuffer) override;
					void Denoise(CommandList& commandList, const DenoiserFrameInfo& frame) override;

					VkImageView GetOutputView() const override { return output->GetView(); }

					// The network reconstructs sub-pixel detail from a known jitter sequence
					JitterMode GetJitterMode() const override { return JitterMode::Halton; }

					// Vulkan extensions NGX needs for this feature; query them before creating the instance / device
					static std::vector<std::string> GetInstanceExtensions();
					static std::vector<std::string> GetDeviceExtensions(VkInstance instance, VkPhysicalDevice physicalDevice);

				private:
					void ReleaseFeature();

					DeviceContext context;
					const GBuffer* gbuffer = nullptr;
					VkExtent2D extent{};

					NVSDK_NGX_Parameter* parameters = nullptr;
					NVSDK_NGX_Handle* feature = nullptr;

					// Converts the renderer's G-buffer into the layout of DLSS's guide buffers
					ComputePass preparePass;

					std::unique_ptr<StorageTexture> normalRoughness;
					std::unique_ptr<StorageTexture> depth;
					std::unique_ptr<StorageTexture> motion;
					std::unique_ptr<StorageTexture> specularHitDistance;
					std::unique_ptr<StorageTexture> output;
				};

			}
		}
	}
}

#endif // FOX_ENABLE_DLSS
