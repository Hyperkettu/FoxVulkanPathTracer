#include "FoxRenderer.h"

// Static member definitions
std::unique_ptr<Fox::Graphics::Managers::Vulkan::PipelineManager> Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::PipelineManager>::instance = nullptr;
std::once_flag Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::PipelineManager>::initFlag;

namespace Fox {

	namespace Graphics {

		namespace Managers {

			namespace Vulkan {

				bool PipelineManager::Initialize(VkDevice device, VkSurfaceCapabilitiesKHR capabilities) {
					
					{
						/**VkDescriptorSetLayout descriptorSetLayouts = Fox::Graphics::Managers::Vulkan::DescriptorManager::Get().GetDescriptorSet(Fox::Graphics::Managers::Vulkan::Descriptor::OFFSCREEN)->GetLayout().Get();

						std::unique_ptr<Fox::Graphics::Vulkan::PipelineLayout> offscreenPipelineLayout = std::make_unique<Fox::Graphics::Vulkan::PipelineLayout>(device,
							std::vector<VkDescriptorSetLayout>{ descriptorSetLayouts },
							std::vector<VkPushConstantRange>{});

						std::vector<VkDynamicState> dynamicStates;
						dynamicStates.push_back(VkDynamicState::VK_DYNAMIC_STATE_VIEWPORT);
						dynamicStates.push_back(VkDynamicState::VK_DYNAMIC_STATE_SCISSOR);

						VkPipelineDynamicStateCreateInfo dynamicState{};
						dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
						dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
						dynamicState.pDynamicStates = dynamicStates.data();
						dynamicState.flags = 0;
						dynamicState.pNext = nullptr;

						VkPipelineRasterizationStateCreateInfo rasterizer{};
						rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
						rasterizer.depthClampEnable = VK_FALSE;
						rasterizer.rasterizerDiscardEnable = VK_FALSE;
						rasterizer.polygonMode = VkPolygonMode::VK_POLYGON_MODE_FILL;
						rasterizer.lineWidth = 1.0f;
						rasterizer.cullMode = VkCullModeFlagBits::VK_CULL_MODE_BACK_BIT;
						rasterizer.frontFace = VkFrontFace::VK_FRONT_FACE_CLOCKWISE;

						rasterizer.depthBiasEnable = VK_FALSE;
						rasterizer.depthBiasConstantFactor = 0.0f;
						rasterizer.depthBiasClamp = 0.0f;
						rasterizer.depthBiasSlopeFactor = 0.0f;

						float viewportX = 0.0f;
						float viewportY = 0.0f;
						float viewportWidth = static_cast<float>(capabilities.currentExtent.width);
						float viewportHeight = static_cast<float>(capabilities.currentExtent.height);
						float viewportMinDepth = 0.0f;
						float viewportMaxDepth = 1.0f;

						VkViewport viewport{};
						viewport.x = viewportX;
						viewport.y = viewportY;
						viewport.width = viewportWidth;
						viewport.height = viewportHeight;
						viewport.minDepth = viewportMinDepth;
						viewport.maxDepth = viewportMaxDepth;

						int32_t scissorX = 0;
						int32_t scissorY = 0;
						VkExtent2D scissorExtent{};
						scissorExtent.width = static_cast<uint32_t>(viewportWidth);
						scissorExtent.height = static_cast<uint32_t>(viewportHeight);

						VkRect2D scissor{};
						scissor.offset = { scissorX, scissorY };
						scissor.extent = scissorExtent;

						VkPipelineViewportStateCreateInfo viewportState;
						viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
						viewportState.viewportCount = 1;
						viewportState.scissorCount = 1;
						viewportState.pViewports = &viewport;
						viewportState.pScissors = &scissor;
						viewportState.pNext = nullptr;
						viewportState.flags = 0;

						std::vector<char> code = Fox::Core::FileSystem::ReadBinaryFile("Shaders/texturedCube.spv");

						VkShaderModuleCreateInfo meshShaderModuleCreateInfo{};
						meshShaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
						meshShaderModuleCreateInfo.codeSize = code.size();
						meshShaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
						VkShaderModule meshShaderModule;
						if (vkCreateShaderModule(device, &meshShaderModuleCreateInfo, nullptr, &meshShaderModule) != VK_SUCCESS) {
							std::cout << "Failed to create shader module for mesh shader" << std::endl;
							exit(1);
						}

						std::vector<char> code2 = Fox::Core::FileSystem::ReadBinaryFile("Shaders/textured_frag.spv");

						VkShaderModuleCreateInfo fragmentShaderModuleCreateInfo{};
						fragmentShaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
						fragmentShaderModuleCreateInfo.codeSize = code2.size();
						fragmentShaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(code2.data());
						VkShaderModule fragmentShaderModule;
						if (vkCreateShaderModule(device, &fragmentShaderModuleCreateInfo, nullptr, &fragmentShaderModule) != VK_SUCCESS) {
							std::cout << "Failed to create shader module for fragment shader" << std::endl;
							exit(1);
						}

						std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

						VkPipelineShaderStageCreateInfo shaderStageInfo{};
						shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
						shaderStageInfo.stage = VkShaderStageFlagBits::VK_SHADER_STAGE_MESH_BIT_EXT;
						shaderStageInfo.module = meshShaderModule;
						shaderStageInfo.pName = "main";

						shaderStages.push_back(shaderStageInfo);

						VkPipelineShaderStageCreateInfo shaderStageInfo2{};
						shaderStageInfo2.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
						shaderStageInfo2.stage = VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT;
						shaderStageInfo2.module = fragmentShaderModule;
						shaderStageInfo2.pName = "main";

						shaderStages.push_back(shaderStageInfo2);

						VkPipelineMultisampleStateCreateInfo multisampling;
						multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
						multisampling.alphaToCoverageEnable = VK_FALSE;
						multisampling.alphaToOneEnable = VK_FALSE;
						multisampling.minSampleShading = 0.0f;
						multisampling.pSampleMask = nullptr;
						multisampling.sampleShadingEnable = VK_FALSE;
						multisampling.rasterizationSamples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
						multisampling.pNext = nullptr;
						multisampling.flags = 0;

						VkPipelineColorBlendAttachmentState colorBlendAttachment{};
						colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
							VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
						colorBlendAttachment.blendEnable = VK_FALSE;

						VkPipelineColorBlendStateCreateInfo colorBlending{};
						colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
						colorBlending.logicOpEnable = VK_FALSE;
						colorBlending.logicOp = VK_LOGIC_OP_COPY;
						colorBlending.attachmentCount = 1;
						colorBlending.pAttachments = &colorBlendAttachment;
						colorBlending.blendConstants[0] = 0.0f;
						colorBlending.blendConstants[1] = 0.0f;
						colorBlending.blendConstants[2] = 0.0f;
						colorBlending.blendConstants[3] = 0.0f;

						VkPipelineDepthStencilStateCreateInfo depthStencil{};
						depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
						depthStencil.pNext = nullptr;
						depthStencil.flags = 0;

						// Enable depth testing
						depthStencil.depthTestEnable = VK_TRUE;

						// Write depth values to the depth buffer
						depthStencil.depthWriteEnable = VK_TRUE;

						// Comparison operation: keep fragment if it’s closer
						depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

						// Optional: depth bounds test
						depthStencil.depthBoundsTestEnable = VK_FALSE;
						depthStencil.minDepthBounds = 0.0f; // must be within [0,1]
						depthStencil.maxDepthBounds = 1.0f;

						// Optional: stencil test (disable for now)
						depthStencil.stencilTestEnable = VK_FALSE;
						depthStencil.front = {};
						depthStencil.back = {};

						Fox::Graphics::Vulkan::PipelineBuilder pipelineBuilder(device);

						std::unique_ptr<Fox::Graphics::Vulkan::Pipeline> offscreenPipeline = std::make_unique<Fox::Graphics::Vulkan::Pipeline>(Fox::Graphics::Vulkan::PipelineBuilder(device)
							.SetShaderStages(shaderStages)
							.SetLayout(offscreenPipelineLayout->Get())
							.SetRenderPass(Fox::Graphics::Managers::Vulkan::RenderPassManager::Get().GetPass(Fox::Graphics::Managers::Vulkan::RenderPass::DEFAULT)->Get())
							.SetViewportState(viewportState)
							.SetRasterizer(rasterizer)
							.SetMultisampling(multisampling)
							.SetColorBlending(colorBlending)
							.SetDepthStencil(depthStencil)
							.Build());

						vkDestroyShaderModule(device, meshShaderModule, nullptr);
						vkDestroyShaderModule(device, fragmentShaderModule, nullptr);

						pipelines[Fox::Graphics::Managers::Vulkan::PipelineCategory::OFFSCREEN_RENDERING] = std::move(offscreenPipeline);
						pipelineLayouts[Fox::Graphics::Managers::Vulkan::PipelineCategory::OFFSCREEN_RENDERING] = std::move(offscreenPipelineLayout);*/

					}

					/*--------------------------------------------------------------------------
					{

						VkDescriptorSetLayout descriptorSetLayouts = Fox::Graphics::Managers::Vulkan::DescriptorManager::Get().GetDescriptorSet(Fox::Graphics::Managers::Vulkan::Descriptor::MAIN_MESH_SHADER)->GetLayout().Get();

						auto mainPipelineLayout = std::make_unique<Fox::Graphics::Vulkan::PipelineLayout>(device,
							std::vector<VkDescriptorSetLayout>{ descriptorSetLayouts },
							std::vector<VkPushConstantRange>{});

						std::vector<char> code = Fox::Core::FileSystem::ReadBinaryFile("Shaders/multi_generic_mesh.spv");

						VkShaderModule mainMeshShaderModule;

						VkShaderModuleCreateInfo mainMeshShaderModuleCreateInfo{};
						mainMeshShaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
						mainMeshShaderModuleCreateInfo.codeSize = code.size();
						mainMeshShaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
						if (vkCreateShaderModule(device, &mainMeshShaderModuleCreateInfo, nullptr, &mainMeshShaderModule) != VK_SUCCESS) {
							std::cout << "Failed to create shader module for mesh shader" << std::endl;
							exit(1);
						}

						std::vector<char> codeIco2 = Fox::Core::FileSystem::ReadBinaryFile("Shaders/textured_multi_generic_mesh.spv");


						VkShaderModuleCreateInfo mainMeshShaderFragmentShaderModuleCreateInfo{};
						mainMeshShaderFragmentShaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
						mainMeshShaderFragmentShaderModuleCreateInfo.codeSize = codeIco2.size();
						mainMeshShaderFragmentShaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(codeIco2.data());
						VkShaderModule mainMeshShaderFragmentShaderModule;
						if (vkCreateShaderModule(device, &mainMeshShaderFragmentShaderModuleCreateInfo, nullptr, &mainMeshShaderFragmentShaderModule) != VK_SUCCESS) {
							std::cout << "Failed to create shader module for fragment shader" << std::endl;
							exit(1);
						}

						std::vector<VkPipelineShaderStageCreateInfo> shaderStagesIco;

						VkPipelineShaderStageCreateInfo shaderStageInfoIco{};
						shaderStageInfoIco.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
						shaderStageInfoIco.stage = VkShaderStageFlagBits::VK_SHADER_STAGE_MESH_BIT_EXT;
						shaderStageInfoIco.module = mainMeshShaderModule;
						shaderStageInfoIco.pName = "main";

						shaderStagesIco.push_back(shaderStageInfoIco);

						VkPipelineShaderStageCreateInfo shaderStageInfoIco2{};
						shaderStageInfoIco2.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
						shaderStageInfoIco2.stage = VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT;
						shaderStageInfoIco2.module = mainMeshShaderFragmentShaderModule;
						shaderStageInfoIco2.pName = "main";

						shaderStagesIco.push_back(shaderStageInfoIco2);

						auto bindingDescription = Fox::Graphics::Vulkan::Vertex::GetBindingDescription();
						auto attributeDescriptions = Fox::Graphics::Vulkan::Vertex::GetAttributeDescriptions();

						float viewportX = 0.0f;
						float viewportY = 0.0f;
						float viewportWidth = static_cast<float>(capabilities.currentExtent.width);
						float viewportHeight = static_cast<float>(capabilities.currentExtent.height);
						float viewportMinDepth = 0.0f;
						float viewportMaxDepth = 1.0f;

						VkViewport viewport{};
						viewport.x = viewportX;
						viewport.y = viewportY;
						viewport.width = viewportWidth;
						viewport.height = viewportHeight;
						viewport.minDepth = viewportMinDepth;
						viewport.maxDepth = viewportMaxDepth;

						int32_t scissorX = 0;
						int32_t scissorY = 0;
						VkExtent2D scissorExtent{};
						scissorExtent.width = static_cast<uint32_t>(viewportWidth);
						scissorExtent.height = static_cast<uint32_t>(viewportHeight);

						VkRect2D scissor{};
						scissor.offset = { scissorX, scissorY };
						scissor.extent = scissorExtent;

						VkPipelineViewportStateCreateInfo viewportState;
						viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
						viewportState.viewportCount = 1;
						viewportState.scissorCount = 1;
						viewportState.pViewports = &viewport;
						viewportState.pScissors = &scissor;
						viewportState.pNext = nullptr;
						viewportState.flags = 0;

						VkPipelineRasterizationStateCreateInfo rasterizer{};
						rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
						rasterizer.depthClampEnable = VK_FALSE;
						rasterizer.rasterizerDiscardEnable = VK_FALSE;
						rasterizer.polygonMode = VkPolygonMode::VK_POLYGON_MODE_FILL;
						rasterizer.lineWidth = 1.0f;
						rasterizer.cullMode = VkCullModeFlagBits::VK_CULL_MODE_BACK_BIT;
						rasterizer.frontFace = VkFrontFace::VK_FRONT_FACE_CLOCKWISE;

						rasterizer.depthBiasEnable = VK_FALSE;
						rasterizer.depthBiasConstantFactor = 0.0f;
						rasterizer.depthBiasClamp = 0.0f;
						rasterizer.depthBiasSlopeFactor = 0.0f;

						VkPipelineMultisampleStateCreateInfo multisampling;
						multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
						multisampling.alphaToCoverageEnable = VK_FALSE;
						multisampling.alphaToOneEnable = VK_FALSE;
						multisampling.minSampleShading = 0.0f;
						multisampling.pSampleMask = nullptr;
						multisampling.sampleShadingEnable = VK_FALSE;
						multisampling.rasterizationSamples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
						multisampling.pNext = nullptr;
						multisampling.flags = 0;

						VkPipelineColorBlendAttachmentState colorBlendAttachment{};
						colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
							VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
						colorBlendAttachment.blendEnable = VK_FALSE;

						VkPipelineColorBlendStateCreateInfo colorBlending{};
						colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
						colorBlending.logicOpEnable = VK_FALSE;
						colorBlending.logicOp = VK_LOGIC_OP_COPY;
						colorBlending.attachmentCount = 1;
						colorBlending.pAttachments = &colorBlendAttachment;
						colorBlending.blendConstants[0] = 0.0f;
						colorBlending.blendConstants[1] = 0.0f;
						colorBlending.blendConstants[2] = 0.0f;
						colorBlending.blendConstants[3] = 0.0f;

						VkPipelineDepthStencilStateCreateInfo depthStencil{};
						depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
						depthStencil.pNext = nullptr;
						depthStencil.flags = 0;

						// Enable depth testing
						depthStencil.depthTestEnable = VK_TRUE;

						// Write depth values to the depth buffer
						depthStencil.depthWriteEnable = VK_TRUE;

						// Comparison operation: keep fragment if it’s closer
						depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

						// Optional: depth bounds test
						depthStencil.depthBoundsTestEnable = VK_FALSE;
						depthStencil.minDepthBounds = 0.0f; // must be within [0,1]
						depthStencil.maxDepthBounds = 1.0f;

						// Optional: stencil test (disable for now)
						depthStencil.stencilTestEnable = VK_FALSE;
						depthStencil.front = {};
						depthStencil.back = {};

						VkPipelineVertexInputStateCreateInfo vertexInfo{};
						vertexInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
						vertexInfo.pNext = nullptr;
						vertexInfo.flags = 0;
						vertexInfo.vertexBindingDescriptionCount = 1;
						vertexInfo.pVertexBindingDescriptions = &bindingDescription;
						vertexInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
						vertexInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

						std::unique_ptr<Fox::Graphics::Vulkan::Pipeline> mainMeshShaderPipeline = std::make_unique<Fox::Graphics::Vulkan::Pipeline>(Fox::Graphics::Vulkan::PipelineBuilder(device)
							.SetShaderStages(shaderStagesIco)
							.SetLayout(mainPipelineLayout->Get())
							.SetRenderPass(Fox::Graphics::Managers::Vulkan::RenderPassManager::Get().GetPass(Fox::Graphics::Managers::Vulkan::RenderPass::DEFAULT)->Get())
							.SetVertexInput(vertexInfo)
							.SetViewportState(viewportState)
							.SetRasterizer(rasterizer)
							.SetMultisampling(multisampling)
							.SetColorBlending(colorBlending)
							.SetDepthStencil(depthStencil)
							.Build());

						pipelines[Fox::Graphics::Managers::Vulkan::PipelineCategory::BASIC_MESH_SHADER] = std::move(mainMeshShaderPipeline);
						pipelineLayouts[Fox::Graphics::Managers::Vulkan::PipelineCategory::BASIC_MESH_SHADER] = std::move(mainPipelineLayout);

						vkDestroyShaderModule(device, mainMeshShaderModule, nullptr);
						vkDestroyShaderModule(device, mainMeshShaderFragmentShaderModule, nullptr); */

					//	return true;


return true;
					
				}

			}
		}
	}
}