#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				// Creates a storage image that is already in VK_IMAGE_LAYOUT_GENERAL. Images made here stay in
				// that layout for their whole life: they are written by ray tracing / compute shaders and may
				// be sampled by a later pass in the same frame.
				std::unique_ptr<StorageTexture> CreateStorageImage(const DeviceContext& context, VkExtent2D extent, VkFormat format);

				// Per-pixel outputs of the path tracer for the current frame. This is everything a denoiser
				// gets to work with. The formats must match the image_format annotations in the shaders.
				struct GBuffer {
					static constexpr VkFormat RADIANCE_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
					static constexpr VkFormat ALBEDO_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
					static constexpr VkFormat SPECULAR_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
					static constexpr VkFormat NORMAL_DEPTH_FORMAT = VK_FORMAT_R32G32B32A32_SFLOAT;
					static constexpr VkFormat MOTION_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;

					GBuffer(const DeviceContext& context, VkExtent2D extent);

					VkExtent2D extent{};

					// rgb: noisy linear HDR radiance of this frame only (no temporal accumulation).
					std::unique_ptr<StorageTexture> radiance;

					// rgb: albedo of the diffuse layer (1 for the sky).
					std::unique_ptr<StorageTexture> albedo;

					// rgb: fraction of light reflected by the specular layer (0 for the sky), a: roughness.
					// Diffuse + specular albedo is what denoisers divide out to separate lighting from texture detail.
					std::unique_ptr<StorageTexture> specular;

					// xyz: world space shading normal, w: distance along the view direction (<= 0 for the sky).
					// Describes the surface seen in a mirror or through glass where there is one (see raygen.slang).
					std::unique_ptr<StorageTexture> normalDepth;

					// xy: offset in pixels to where this surface point was in the previous frame,
					// z: the depth it had in the previous frame (used to validate reprojection),
					// w: distance from the surface to what its mirror reflection shows (65504 = nothing),
					//    only filled in when the denoiser does not want reflected surfaces.
					std::unique_ptr<StorageTexture> motion;
				};

			}
		}
	}
}
