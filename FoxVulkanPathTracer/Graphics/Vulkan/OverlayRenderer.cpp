#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace {
				// Must match Shaders/overlay_vert.slang
				struct OverlayConstants {
					glm::vec2 viewportSize;
				};
			}

			OverlayRenderer::OverlayRenderer(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass, uint32_t framesInFlight)
				: device(device)
			{
				pipelineLayout = std::make_unique<PipelineLayout>(device, std::vector<VkDescriptorSetLayout>{},
					std::vector<VkPushConstantRange>{ { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(OverlayConstants) } });

				// Interleaved: position (2 floats), colour (4 floats)
				static const VkVertexInputBindingDescription binding{ 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
				static const VkVertexInputAttributeDescription attributes[2] = {
					{ 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position) },
					{ 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color) },
				};

				VkPipelineVertexInputStateCreateInfo vertexInput{};
				vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
				vertexInput.vertexBindingDescriptionCount = 1;
				vertexInput.pVertexBindingDescriptions = &binding;
				vertexInput.vertexAttributeDescriptionCount = 2;
				vertexInput.pVertexAttributeDescriptions = attributes;

				pipeline = std::make_unique<Pipeline>(PipelineBuilder(device)
					.WithShader(Fox::Graphics::Enums::PipelineShaderStage::VERTEX_SHADER, "Shaders/overlay_vert.spv")
					.WithShader(Fox::Graphics::Enums::PipelineShaderStage::FRAGMENT_SHADER, "Shaders/overlay_frag.spv")
					.SetVertexInput(vertexInput)
					.WithRasterization(VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, 1.0f, VK_CULL_MODE_NONE)
					.WithDepthStencil(VK_FALSE, VK_FALSE)
					.WithColorBlending(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, VK_TRUE)
					// Placeholders: viewport and scissor are dynamic, set when recording
					.WithViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f)
					.WithScissor(0, 0, 1, 1)
					.SetLayout(pipelineLayout->Get())
					.SetRenderPass(renderPass)
					.Build());

				for (uint32_t i = 0; i < framesInFlight; i++) {
					vertexBuffers.push_back(std::make_unique<Buffer>(device, physicalDevice, sizeof(Vertex) * MAX_VERTICES,
						VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
				}
			}

			OverlayRenderer::~OverlayRenderer() = default;

			void OverlayRenderer::Record(CommandList& commandList, const std::vector<Vertex>& vertices, uint32_t frameSlot, VkExtent2D extent) {
				const uint32_t count = static_cast<uint32_t>(std::min<size_t>(vertices.size() / 3 * 3, MAX_VERTICES));
				if (count == 0 || frameSlot >= vertexBuffers.size()) {
					return;
				}

				// This frame slot's buffer: the GPU finished reading it (the slot's fence was waited on)
				Buffer& buffer = *vertexBuffers[frameSlot];
				std::memcpy(buffer.Map(), vertices.data(), sizeof(Vertex) * count);
				buffer.Unmap();

				const OverlayConstants constants{ glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height)) };
				const VkBuffer vertexBuffer = buffer.Get();
				const VkDeviceSize offset = 0;

				commandList
					.SetViewport(0, 0, extent.width, extent.height)
					.SetScissor(0, 0, extent.width, extent.height)
					.BindPipeline(pipeline->Get())
					.PushConstants(pipelineLayout->Get(), VK_SHADER_STAGE_VERTEX_BIT, sizeof(constants), &constants);

				vkCmdBindVertexBuffers(commandList.Get(), 0, 1, &vertexBuffer, &offset);
				vkCmdDraw(commandList.Get(), count, 1, 0, 0);
			}
		}
	}
}
