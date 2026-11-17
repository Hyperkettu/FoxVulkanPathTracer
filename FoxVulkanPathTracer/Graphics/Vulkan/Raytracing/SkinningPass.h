#pragma once

#include <memory>

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				// Compute pass that deforms skinned meshes (Shaders/skinning.slang). Owns the pipeline and one
				// descriptor set pointing at the scene's buffers; Dispatch records one skinned mesh.
				class SkinningPass {
				public:
					// Must match SkinningConstants in Shaders/skinning.slang
					struct Constants {
						uint32_t firstVertex;
						uint32_t vertexCount;
						uint32_t firstSkinVertex;
						uint32_t firstJoint;
					};

					SkinningPass(VkDevice device,
						std::unique_ptr<Buffer>& bindPose,
						std::unique_ptr<Buffer>& joints,
						std::unique_ptr<Buffer>& vertices,
						std::unique_ptr<VertexBuffer>& buildVertices,
						std::unique_ptr<Buffer>& previousPositions);
					~SkinningPass();

					SkinningPass(const SkinningPass&) = delete;
					SkinningPass& operator=(const SkinningPass&) = delete;

					void Dispatch(VkCommandBuffer cmd, const Constants& constants) const;

				private:
					VkDevice device = VK_NULL_HANDLE;
					VkPipeline pipeline = VK_NULL_HANDLE;
					std::unique_ptr<DescriptorSetBuilder> descriptors;
					std::unique_ptr<PipelineLayout> layout;
					std::unique_ptr<DescriptorSet> set;
				};
			}
		}
	}
}
