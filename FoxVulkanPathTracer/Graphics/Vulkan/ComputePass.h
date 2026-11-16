#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			// A compute shader that works on screen sized storage images: owns the pipeline, its layout
			// and one or more descriptor sets (several sets allow ping-ponging between images).
			//
			// The shader is expected to declare its images as bindings 0..N-1 of set 0, use
			// [numthreads(8, 8, 1)] and optionally one push constant block.
			class ComputePass {
			public:
				static constexpr uint32_t GROUP_SIZE = 8;

				ComputePass(VkDevice device, const std::string& shaderPath, uint32_t storageImageCount, uint32_t setCount = 1, uint32_t pushConstantSize = 0);
				~ComputePass();

				ComputePass(const ComputePass&) = delete;
				ComputePass& operator=(const ComputePass&) = delete;

				// Points every binding of the given set at an image. views.size() must equal storageImageCount.
				void BindImages(uint32_t setIndex, const std::vector<VkImageView>& views);

				// Runs the shader over every pixel of extent. pushConstants must point at pushConstantSize bytes.
				void Dispatch(CommandList& commandList, VkExtent2D extent, uint32_t setIndex = 0, const void* pushConstants = nullptr) const;

			private:
				VkDevice device = VK_NULL_HANDLE;
				VkPipeline pipeline = VK_NULL_HANDLE;

				uint32_t storageImageCount = 0;
				uint32_t pushConstantSize = 0;

				std::unique_ptr<DescriptorSetBuilder> descriptors; // Layout + pool
				std::unique_ptr<PipelineLayout> layout;
				std::vector<DescriptorSet> sets;
			};

		}
	}
}
