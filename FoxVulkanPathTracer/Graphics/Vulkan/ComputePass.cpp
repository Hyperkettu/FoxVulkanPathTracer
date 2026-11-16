#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			ComputePass::ComputePass(VkDevice device, const std::string& shaderPath, uint32_t storageImageCount, uint32_t setCount, uint32_t pushConstantSize)
				: device(device), storageImageCount(storageImageCount), pushConstantSize(pushConstantSize)
			{
				descriptors = std::make_unique<DescriptorSetBuilder>(device);
				for (uint32_t binding = 0; binding < storageImageCount; binding++) {
					descriptors->AddBinding(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
				}
				descriptors->SetMaxSets(setCount).Build();

				std::vector<VkPushConstantRange> pushConstantRanges;
				if (pushConstantSize > 0) {
					pushConstantRanges.push_back({ VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantSize });
				}

				layout = std::make_unique<PipelineLayout>(device, std::vector<VkDescriptorSetLayout>{ descriptors->GetLayout().Get() }, pushConstantRanges);

				sets.reserve(setCount);
				for (uint32_t i = 0; i < setCount; i++) {
					sets.push_back(descriptors->AllocateSet());
				}

				// Shader module is only needed while the pipeline is created
				const std::vector<char> code = Fox::Core::FileSystem::ReadBinaryFile(shaderPath);

				VkShaderModuleCreateInfo moduleInfo{};
				moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				moduleInfo.codeSize = code.size();
				moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

				VkShaderModule shaderModule = VK_NULL_HANDLE;
				if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
					throw std::runtime_error("Failed to create compute shader module: " + shaderPath);
				}

				VkComputePipelineCreateInfo pipelineInfo{};
				pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
				pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
				pipelineInfo.stage.module = shaderModule;
				pipelineInfo.stage.pName = "main";
				pipelineInfo.layout = layout->Get();

				const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
				vkDestroyShaderModule(device, shaderModule, nullptr);

				if (result != VK_SUCCESS) {
					throw std::runtime_error("Failed to create compute pipeline: " + shaderPath);
				}
			}

			ComputePass::~ComputePass() {
				if (pipeline) {
					vkDestroyPipeline(device, pipeline, nullptr);
				}
			}

			void ComputePass::BindImages(uint32_t setIndex, const std::vector<VkImageView>& views) {
				if (views.size() != storageImageCount) {
					throw std::runtime_error("ComputePass::BindImages: view count does not match the shader's binding count.");
				}

				DescriptorSet& set = sets[setIndex];
				set.ClearWrites();
				set.Reserve(storageImageCount); // The set keeps pointers into its info arrays, they must not reallocate

				for (uint32_t binding = 0; binding < storageImageCount; binding++) {
					set.SetStorageImage(binding, views[binding]);
				}

				set.Update();
			}

			void ComputePass::Dispatch(CommandList& commandList, VkExtent2D extent, uint32_t setIndex, const void* pushConstants) const {
				commandList
					.BindPipeline(pipeline, VK_PIPELINE_BIND_POINT_COMPUTE)
					.BindDescriptorSets(layout->Get(), 0, { sets[setIndex].Get() }, VK_PIPELINE_BIND_POINT_COMPUTE);

				if (pushConstantSize > 0 && pushConstants) {
					commandList.PushConstants(layout->Get(), VK_SHADER_STAGE_COMPUTE_BIT, pushConstantSize, pushConstants);
				}

				commandList.Dispatch(
					(extent.width + GROUP_SIZE - 1) / GROUP_SIZE,
					(extent.height + GROUP_SIZE - 1) / GROUP_SIZE,
					1);
			}

		}
	}
}
