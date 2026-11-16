#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				std::unique_ptr<StorageTexture> CreateStorageImage(const DeviceContext& context, VkExtent2D extent, VkFormat format) {
					auto texture = std::make_unique<StorageTexture>(
						context.device,
						context.physicalDevice,
						VkExtent3D{ extent.width, extent.height, 1 },
						format,
						VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
						VK_IMAGE_ASPECT_COLOR_BIT);

					const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
					const VkClearColorValue black{ { 0.0f, 0.0f, 0.0f, 0.0f } };

					// Clear as well: history buffers are read before they are first written.
					CommandList commandList(context.device, context.setupCommandPool);
					commandList.Begin()
						.ImageBarrier(texture->GetImage(), range,
							VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
							0, VK_ACCESS_TRANSFER_WRITE_BIT,
							VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT)
						.ClearColorImage(texture->GetImage(), VK_IMAGE_LAYOUT_GENERAL, black, range)
						.End()
						.SubmitAndWait(context.graphicsQueue);

					return texture;
				}

				GBuffer::GBuffer(const DeviceContext& context, VkExtent2D extent) : extent(extent) {
					radiance = CreateStorageImage(context, extent, RADIANCE_FORMAT);
					albedo = CreateStorageImage(context, extent, ALBEDO_FORMAT);
					specular = CreateStorageImage(context, extent, SPECULAR_FORMAT);
					normalDepth = CreateStorageImage(context, extent, NORMAL_DEPTH_FORMAT);
					motion = CreateStorageImage(context, extent, MOTION_FORMAT);
				}

			}
		}
	}
}
