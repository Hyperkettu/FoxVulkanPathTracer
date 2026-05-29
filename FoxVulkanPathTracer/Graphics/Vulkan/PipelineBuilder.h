#pragma once

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

                Fox::Graphics::Vulkan::Pipeline Build() const {
                    VkGraphicsPipelineCreateInfo info{};
                    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                    info.stageCount = static_cast<uint32_t>(shaderStages.size());
                    info.pStages = shaderStages.data();
                    info.pVertexInputState = &vertexInput;
                    info.pInputAssemblyState = &inputAssembly;
                    info.pViewportState = &viewportState;
                    info.pRasterizationState = &rasterizer;
                    info.pMultisampleState = &multisampling;
                    info.pColorBlendState = &colorBlending;
                    info.pDepthStencilState = &depthStencil;
                    info.layout = pipelineLayout;
                    info.renderPass = renderPass;
                    info.subpass = 0;

                    VkPipeline pipeline;
                    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS)
                        throw std::runtime_error("Failed to create graphics pipeline!");

                    return Fox::Graphics::Vulkan::Pipeline(device, pipelineLayout, pipeline);
                }

            private:
                VkDevice device;
                VkRenderPass renderPass = VK_NULL_HANDLE;
                VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

                std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
                VkPipelineVertexInputStateCreateInfo vertexInput{};
                VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
                VkPipelineViewportStateCreateInfo viewportState{};
                VkPipelineRasterizationStateCreateInfo rasterizer{};
                VkPipelineMultisampleStateCreateInfo multisampling{};
                VkPipelineDepthStencilStateCreateInfo depthStencil{};
                VkPipelineColorBlendStateCreateInfo colorBlending{};
            };
        } 
    }
}