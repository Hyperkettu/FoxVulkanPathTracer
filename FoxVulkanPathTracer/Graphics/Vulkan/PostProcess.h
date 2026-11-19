#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			// Tone maps the path tracer's linear HDR output into the swapchain image with a full-screen pass.
			class PostProcessor {
			public:
				PostProcessor(VkDevice device);
				~PostProcessor();

				PostProcessor(const PostProcessor&) = delete;
				PostProcessor& operator=(const PostProcessor&) = delete;

				// Points the pass at the HDR image it should read. Call again whenever that image is recreated.
				void SetInput(VkImageView hdrImageView, VkImageLayout layout);

				// Records the tone mapping render pass. The caller owns Begin/End/Submit of the command list.
				// overlay, if given, records more drawing into the same pass on top of the image (e.g. gizmos).
				void Record(Fox::Graphics::Vulkan::CommandList& commandList, VkFramebuffer framebuffer, VkExtent2D extent,
					const std::function<void(Fox::Graphics::Vulkan::CommandList&)>& overlay = {});

				// The grade applied with the tone mapping (saved with the scene: MainRayTracingScene::Environment)
				struct Grade {
					float exposure = 0.0f;   // Stops (EV): each +1 doubles the light, before the tone curve
					float brightness = 0.0f; // -1..1, added to the displayed image
					float contrast = 1.0f;   // About mid grey: 1 unchanged
					float saturation = 1.0f; // 0 grey, 1 unchanged
				};
				void SetGrade(const Grade& newGrade) { grade = newGrade; }
				const Grade& GetGrade() const { return grade; }

				// Exposure in stops (EV): each +1 doubles the brightness of the image.
				void SetExposure(float stops) { grade.exposure = stops; }
				float GetExposure() const { return grade.exposure; }

				// Size of the fragment shader's push constant block (see tonemapping_frag.slang)
				static constexpr uint32_t PUSH_CONSTANT_SIZE = 4 * sizeof(float);

			private:
				VkDevice device = VK_NULL_HANDLE;
				VkSampler sampler = VK_NULL_HANDLE;
				Grade grade;

				std::unique_ptr<Fox::Graphics::Vulkan::DescriptorSet> descriptorSet;
			};

		}
	}
}
