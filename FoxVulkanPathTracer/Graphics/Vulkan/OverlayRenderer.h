#pragma once

#include <memory>
#include <vector>

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			// Draws 2D triangles in pixel coordinates over the final image, alpha blended: editor gizmos and
			// other user interface. Recorded inside the tone mapping render pass (see PostProcessor::Record).
			class OverlayRenderer {
			public:
				// Matches Fox::Editor::TransformGizmo::OverlayVertex and Shaders/overlay_vert.slang
				struct Vertex {
					glm::vec2 position; // Pixels, (0, 0) at the top left
					glm::vec4 color;    // Straight alpha
				};

				static constexpr uint32_t MAX_VERTICES = 65536;

				OverlayRenderer(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass, uint32_t framesInFlight);
				~OverlayRenderer();

				OverlayRenderer(const OverlayRenderer&) = delete;
				OverlayRenderer& operator=(const OverlayRenderer&) = delete;

				// Uploads the triangles (three vertices each) of this frame and draws them. frameSlot: the frame in flight.
				void Record(CommandList& commandList, const std::vector<Vertex>& vertices, uint32_t frameSlot, VkExtent2D extent);

			private:
				VkDevice device = VK_NULL_HANDLE;
				std::unique_ptr<PipelineLayout> pipelineLayout;
				std::unique_ptr<Pipeline> pipeline;
				std::vector<std::unique_ptr<Buffer>> vertexBuffers; // One per frame in flight
			};
		}
	}
}
