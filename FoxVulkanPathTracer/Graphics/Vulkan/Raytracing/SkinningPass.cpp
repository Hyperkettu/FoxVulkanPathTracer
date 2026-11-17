#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				namespace {
					const uint32_t GROUP_SIZE = 64; // numthreads in skinning.slang
				}

				SkinningPass::SkinningPass(VkDevice device,
					std::unique_ptr<Buffer>& bindPose,
					std::unique_ptr<Buffer>& joints,
					std::unique_ptr<Buffer>& vertices,
					std::unique_ptr<VertexBuffer>& buildVertices,
					std::unique_ptr<Buffer>& previousPositions)
					: device(device)
				{
					descriptors = std::make_unique<DescriptorSetBuilder>(device);
					for (uint32_t binding = 0; binding < 5; binding++) {
						descriptors->AddBinding(binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
					}
					descriptors->SetMaxSets(1).Build();

					layout = std::make_unique<PipelineLayout>(device, std::vector<VkDescriptorSetLayout>{ descriptors->GetLayout().Get() },
						std::vector<VkPushConstantRange>{ { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } });

					set = std::make_unique<DescriptorSet>(descriptors->AllocateSet());
					set->Reserve(5);
					set->SetStorageBuffer(0, bindPose);
					set->SetStorageBuffer(1, joints);
					set->SetStorageBuffer(2, vertices);
					set->SetVertexBuffer<Vertex>(3, buildVertices);
					set->SetStorageBuffer(4, previousPositions);
					set->Update();

					const std::vector<char> code = Fox::Core::FileSystem::ReadBinaryFile("Shaders/skinning.spv");

					VkShaderModuleCreateInfo moduleInfo{};
					moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
					moduleInfo.codeSize = code.size();
					moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

					VkShaderModule shaderModule = VK_NULL_HANDLE;
					if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
						throw std::runtime_error("Failed to create the skinning shader module");
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
						throw std::runtime_error("Failed to create the skinning pipeline");
					}
				}

				SkinningPass::~SkinningPass() {
					if (pipeline) {
						vkDestroyPipeline(device, pipeline, nullptr);
					}
				}

				void SkinningPass::Dispatch(VkCommandBuffer cmd, const Constants& constants) const {
					if (constants.vertexCount == 0) {
						return;
					}

					const VkDescriptorSet descriptorSet = set->Get();
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout->Get(), 0, 1, &descriptorSet, 0, nullptr);
					vkCmdPushConstants(cmd, layout->Get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &constants);
					vkCmdDispatch(cmd, (constants.vertexCount + GROUP_SIZE - 1) / GROUP_SIZE, 1, 1);
				}
			}
		}
	}
}
