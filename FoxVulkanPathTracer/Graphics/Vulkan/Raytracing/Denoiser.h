#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				// How the primary ray of each pixel should be offset from the pixel center.
				enum class JitterMode {
					None,   // Always through the pixel center: stable G-buffer, no anti-aliasing from jitter
					Random, // New random sub-pixel offset every frame (for plain accumulation)
					Halton  // Low discrepancy sequence, reported to the denoiser (temporal upscalers / DLSS)
				};

				// Per-frame information handed to the active denoiser.
				struct DenoiserFrameInfo {
					uint32_t frameIndex = 0;

					// True when the previous frame cannot be reused at all (first frame, resize, denoiser switched).
					bool resetHistory = false;

					// True when the view changed since the previous frame.
					bool cameraMoved = false;

					// Sub-pixel offset of the primary rays in pixels, in [-0.5, 0.5].
					glm::vec2 jitter{ 0.0f };

					// Camera of this frame, for denoisers that work with matrices instead of the motion buffer.
					// Right-handed view space (looking down -Z), depth range 0..1.
					glm::mat4 view{ 1.0f };
					glm::mat4 projection{ 1.0f };
					float aspectRatio = 1.0f;
					float nearPlane = 0.1f;
					float farPlane = 1000.0f;
					float verticalFov = 0.0f; // Radians

					float deltaTime = 0.0f; // Seconds
				};

				// Turns the noisy output of the path tracer (the G-buffer) into the image that gets tone mapped.
				//
				// To add a denoiser: derive from this class and register it in CreateDenoisers() (Denoiser.cpp).
				// The renderer treats all denoisers the same; the user cycles through them at runtime.
				class Denoiser {
				public:
					virtual ~Denoiser() = default;

					virtual const char* GetName() const = 0;

					// (Re)creates everything that depends on the render resolution and binds the G-buffer.
					// Called once after construction and again whenever the G-buffer is recreated.
					virtual void Resize(const GBuffer& gbuffer) = 0;

					// Records the denoising passes. On entry the G-buffer has been written by the ray tracing
					// pipeline and made visible to compute shaders; all images are in VK_IMAGE_LAYOUT_GENERAL.
					virtual void Denoise(CommandList& commandList, const DenoiserFrameInfo& frame) = 0;

					// The linear HDR result of Denoise(), in VK_IMAGE_LAYOUT_GENERAL. Only changes on Resize().
					virtual VkImageView GetOutputView() const = 0;

					virtual JitterMode GetJitterMode() const { return JitterMode::None; }

					// What the G-buffer should contain where the camera looks into a mirror:
					//   true:  the surface seen in the reflection. For filters that know one surface per pixel and
					//          would otherwise blur reflections, because a mirror itself has no detail.
					//   false: the mirror itself, with the distance to the reflected surface stored next to the
					//          motion vectors. For denoisers that reproject reflections themselves.
					virtual bool WantsReflectedSurfaces() const { return false; }
				};

				// Builds every denoiser that can run on this device, in the order they are cycled through.
				std::vector<std::unique_ptr<Denoiser>> CreateDenoisers(const DeviceContext& context);

				// Vulkan extensions the denoisers would like to have enabled. They are optional: the renderer
				// enables the ones that exist, and a denoiser that misses something is simply not offered.
				std::vector<std::string> GetDenoiserInstanceExtensions();
				std::vector<std::string> GetDenoiserDeviceExtensions(VkInstance instance, VkPhysicalDevice physicalDevice);

			}
		}
	}
}
