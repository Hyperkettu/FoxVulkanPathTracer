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
				void Record(Fox::Graphics::Vulkan::CommandList& commandList, VkFramebuffer framebuffer, VkExtent2D extent);

				// Exposure in stops (EV): each +1 doubles the brightness of the image.
				void SetExposure(float stops) { exposureStops = stops; }
				float GetExposure() const { return exposureStops; }

				// Size of the fragment shader's push constant block (see tonemapping_frag.slang)
				static constexpr uint32_t PUSH_CONSTANT_SIZE = sizeof(float);

			private:
				VkDevice device = VK_NULL_HANDLE;
				VkSampler sampler = VK_NULL_HANDLE;
				float exposureStops = 0.0f;

				std::unique_ptr<Fox::Graphics::Vulkan::DescriptorSet> descriptorSet;
			};

		}
	}
}
