#pragma once

#include "FoxVulkanPathTracer/Graphics/Enums/PipelineShaderStage.h"  

namespace Fox
{
    namespace Graphics
    {
        namespace Vulkan
        {
            class PipelineBuilder {
            public:
                explicit PipelineBuilder(VkDevice device)
                    : device(device) {
                }

                PipelineBuilder& SetShaderStages(const std::vector<VkPipelineShaderStageCreateInfo>& stages) {
                    shaderStages = stages;
                    return *this;
                }

                PipelineBuilder& SetLayout(VkPipelineLayout layout) {
                    pipelineLayout = layout;
                    return *this;
                }

                PipelineBuilder& SetRenderPass(VkRenderPass rp) {
                    renderPass = rp;
                    return *this;
                }

                PipelineBuilder& SetVertexInput(const VkPipelineVertexInputStateCreateInfo& vi) {
                    vertexInput = vi;
                    return *this;
                }

                PipelineBuilder& SetViewportState(const VkPipelineViewportStateCreateInfo& vs) {
                    viewportState = vs;
                    return *this;
                }

                PipelineBuilder& SetRasterizer(const VkPipelineRasterizationStateCreateInfo& rs) {
                    rasterizer = rs;
                    return *this;
                }

                PipelineBuilder& SetMultisampling(const VkPipelineMultisampleStateCreateInfo& ms) {
                    multisampling = ms;
                    return *this;
                }

                PipelineBuilder& SetColorBlending(const VkPipelineColorBlendStateCreateInfo& cb) {
                    colorBlending = cb;
                    return *this;
                }

                PipelineBuilder& SetDepthStencil(const VkPipelineDepthStencilStateCreateInfo& ds) {
                    depthStencil = ds;
                    return *this;
                }

                Fox::Graphics::Vulkan::PipelineBuilder& WithShader(Fox::Graphics::Enums::PipelineShaderStage stage, const std::string& shaderPath);
                Fox::Graphics::Vulkan::PipelineBuilder& WithViewport(float viewportX, float viewportY, float width, float height, float minDepth, float maxDepth);
                Fox::Graphics::Vulkan::PipelineBuilder& WithScissor(int32_t scissorX, int32_t scissorY, uint32_t width, uint32_t height);


                void DestroyShaderModules();

                static VkShaderStageFlagBits ConvertShaderStage(Fox::Graphics::Enums::PipelineShaderStage stage);

                Fox::Graphics::Vulkan::Pipeline Build() {

                    std::vector<VkDynamicState> dynamicStates;

                    if (withViewport || withScissor) {
                        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                        viewportState.viewportCount = 1;
                        viewportState.scissorCount = 1;
                        viewportState.pViewports = withViewport ? &viewport : nullptr;
                        viewportState.pScissors = withScissor ? &scissor : nullptr;
                        viewportState.pNext = nullptr;
                        viewportState.flags = 0;

                        if (withViewport) {
                            dynamicStates.push_back(VkDynamicState::VK_DYNAMIC_STATE_VIEWPORT);
                        }
                        if (withScissor) {
                            dynamicStates.push_back(VkDynamicState::VK_DYNAMIC_STATE_SCISSOR);
                        }

                        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
                        dynamicState.pDynamicStates = dynamicStates.data();
                        dynamicState.flags = 0;
                        dynamicState.pNext = nullptr;
                    }

                    if (withDefaultRasterization) {
                        WithRasterization();
                    }

                    if (withDefaultMultisampling) {
                        WithMultisampling();
                    }

                    if (withDefaultBlending) {
                        WithColorBlending();
                    }

                    if (withDefaultDepthStencil) {
                        WithDepthStencil();
                    }

                    VkGraphicsPipelineCreateInfo info{};
                    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                    info.stageCount = static_cast<uint32_t>(shaderStages.size());
                    info.pStages = shaderStages.data();
                    info.pVertexInputState = withVertexInput ? &vertexInput : nullptr;
                    info.pInputAssemblyState = &inputAssembly;
                    info.pViewportState = &viewportState;
                    info.pRasterizationState = &rasterizer;
                    info.pMultisampleState = &multisampling;
                    info.pColorBlendState = &colorBlending;
                    info.pDepthStencilState = &depthStencil;
                    info.pDynamicState = withScissor || withViewport ? &dynamicState : nullptr;
                    info.layout = pipelineLayout;
                    info.renderPass = renderPass;
                    info.subpass = 0;

                    VkPipeline pipeline;
                    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS)
                        throw std::runtime_error("Failed to create graphics pipeline!");

                    DestroyShaderModules();

                    return Fox::Graphics::Vulkan::Pipeline(device, pipelineLayout, pipeline);
                }

                Fox::Graphics::Vulkan::PipelineBuilder& WithRasterization(VkBool32 depthClampEnable = VK_FALSE, VkBool32 rasterizationDiscardEnable = VK_FALSE, VkPolygonMode polygonMode = VkPolygonMode::VK_POLYGON_MODE_FILL, float lineWidth = 1.0f, VkCullModeFlagBits cullMode = VkCullModeFlagBits::VK_CULL_MODE_BACK_BIT, VkFrontFace frontFace = VkFrontFace::VK_FRONT_FACE_CLOCKWISE, VkBool32 depthBiasEnable = VK_FALSE, float depthBiasConstantFactor = 0.0f, float depthBiasClamp = 0.0f, float depthBiasSlopeFactor = 0.0f) {
                    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                    rasterizer.depthClampEnable = depthClampEnable;
                    rasterizer.rasterizerDiscardEnable = rasterizationDiscardEnable;
                    rasterizer.polygonMode = polygonMode;
                    rasterizer.lineWidth = lineWidth;
                    rasterizer.cullMode = cullMode;
                    rasterizer.frontFace = frontFace;

                    rasterizer.depthBiasEnable = depthBiasEnable;
                    rasterizer.depthBiasConstantFactor = depthBiasConstantFactor;
                    rasterizer.depthBiasClamp = depthBiasClamp;
                    rasterizer.depthBiasSlopeFactor = depthBiasSlopeFactor;

                    withDefaultRasterization = false;

                    return *this;
                }

                Fox::Graphics::Vulkan::PipelineBuilder& WithMultisampling(VkSampleCountFlagBits sampleCount = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT, VkBool32 alphaToCoverageEnable = VK_FALSE, VkBool32 alphaToOneEnable = VK_FALSE, VkBool32 sampleShadingEnable = VK_FALSE, float minSampleShading = 0.0f, const VkSampleMask* sampleMask = nullptr) {
                    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                    multisampling.alphaToCoverageEnable = alphaToCoverageEnable;
                    multisampling.alphaToOneEnable = alphaToOneEnable;
                    multisampling.minSampleShading = minSampleShading;
                    multisampling.pSampleMask = sampleMask;
                    multisampling.sampleShadingEnable = sampleShadingEnable;
                    multisampling.rasterizationSamples = sampleCount;
                    multisampling.pNext = nullptr;
                    multisampling.flags = 0;

                    withDefaultMultisampling = false;

                    return *this;
                }

                Fox::Graphics::Vulkan::PipelineBuilder WithColorBlending(VkColorComponentFlags colorWriteMask = VkColorComponentFlagBits::VK_COLOR_COMPONENT_R_BIT | VkColorComponentFlagBits::VK_COLOR_COMPONENT_G_BIT |
                    VkColorComponentFlagBits::VK_COLOR_COMPONENT_B_BIT | VkColorComponentFlagBits::VK_COLOR_COMPONENT_A_BIT, VkBool32 blendEnable = VK_FALSE, VkBool32 logicOpEnable = VK_FALSE, VkLogicOp logicOp = VkLogicOp::VK_LOGIC_OP_COPY, std::vector<float> blendConstants = { 0.0f, 0.0f, 0.0f, 0.0f }) {
                    colorBlendAttachment.colorWriteMask = colorWriteMask;
                    colorBlendAttachment.blendEnable = blendEnable;
                    colorBlendAttachment.colorBlendOp = VkBlendOp::VK_BLEND_OP_ADD;
                    colorBlendAttachment.srcColorBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_SRC_ALPHA;
                    colorBlendAttachment.dstColorBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                    colorBlendAttachment.alphaBlendOp = VkBlendOp::VK_BLEND_OP_ADD;
                    colorBlendAttachment.srcAlphaBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE;
                    colorBlendAttachment.dstAlphaBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ZERO;

                    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                    colorBlending.logicOpEnable = logicOpEnable;
                    colorBlending.logicOp = logicOp;
                    colorBlending.attachmentCount = 1;
                    colorBlending.pAttachments = &colorBlendAttachment;
                    colorBlending.blendConstants[0] = blendConstants[0];
                    colorBlending.blendConstants[1] = blendConstants[1];
                    colorBlending.blendConstants[2] = blendConstants[2];
                    colorBlending.blendConstants[3] = blendConstants[3];



                    withDefaultBlending = false;

                    return *this;
                }

                Fox::Graphics::Vulkan::PipelineBuilder WithDepthStencil(
                    VkBool32 depthTestEnable = VK_TRUE,
                    VkBool32 depthWriteEnable = VK_TRUE,
                    VkCompareOp depthCompareOp = VkCompareOp::VK_COMPARE_OP_LESS,
                    VkBool32 depthBoundsTestEnable = VK_FALSE,
                    float minDepthBounds = 0.0f,
                    float maxDepthBounds = 0.0f,
                    VkBool32 stencilTestEnable = VK_FALSE) {
                    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                    depthStencil.pNext = nullptr;
                    depthStencil.flags = 0;

                    // Enable depth testing
                    depthStencil.depthTestEnable = depthTestEnable;

                    // Write depth values to the depth buffer
                    depthStencil.depthWriteEnable = depthWriteEnable;

                    // Comparison operation: keep fragment if it’s closer
                    depthStencil.depthCompareOp = depthCompareOp;

                    // Optional: depth bounds test
                    depthStencil.depthBoundsTestEnable = depthBoundsTestEnable;
                    depthStencil.minDepthBounds = minDepthBounds; // must be within [0,1]
                    depthStencil.maxDepthBounds = maxDepthBounds;

                    // Optional: stencil test (disable for now)
                    depthStencil.stencilTestEnable = stencilTestEnable;
                    depthStencil.front = {};
                    depthStencil.back = {};

                    withDefaultDepthStencil = false;

                    return *this;
                }
                template<class VertexType>
                Fox::Graphics::Vulkan::PipelineBuilder& WithVertexInput()
                {
                    auto bindingDescription = VertexType::GetBindingDescription();
                    auto attributeDescriptions = VertexType::GetAttributeDescriptions();

                    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                    vertexInput.pNext = nullptr;
                    vertexInput.flags = 0;
                    vertexInput.vertexBindingDescriptionCount = 1;
                    vertexInput.pVertexBindingDescriptions = &bindingDescription;
                    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
                    vertexInput.pVertexAttributeDescriptions = attributeDescriptions.data();

                    withVertexInput = true;

                    return *this;
                }


            private:
                VkDevice device;
                VkRenderPass renderPass = VK_NULL_HANDLE;
                VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

                std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
                std::vector<VkShaderModuleCreateInfo> shaderModuleCreateInfos;
                std::vector<VkShaderModule> shaderModules;

                VkPipelineVertexInputStateCreateInfo vertexInput{};
                VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
                VkPipelineViewportStateCreateInfo viewportState{};
                VkPipelineRasterizationStateCreateInfo rasterizer{};
                VkPipelineMultisampleStateCreateInfo multisampling{};
                VkPipelineDepthStencilStateCreateInfo depthStencil{};
                VkPipelineColorBlendStateCreateInfo colorBlending{};
                VkPipelineColorBlendAttachmentState colorBlendAttachment{};
                VkPipelineDynamicStateCreateInfo dynamicState{};


                VkViewport viewport{};
                bool withViewport = false;

                VkExtent2D scissorExtent{};
                VkRect2D scissor{};
                bool withScissor = false;

                bool withDefaultRasterization = true;
                bool withDefaultMultisampling = true;
                bool withDefaultBlending = true;
                bool withDefaultDepthStencil = true;
                bool withVertexInput = false;

            };
        }
    }
}