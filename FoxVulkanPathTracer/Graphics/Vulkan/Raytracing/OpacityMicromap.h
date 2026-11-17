#pragma once

#include <array>
#include <map>
#include <memory>
#include <vector>

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				// CPU copy of an alpha channel, for building opacity micromaps
				struct AlphaMask {
					uint32_t width = 0;
					uint32_t height = 0;
					std::vector<uint8_t> alpha;

					// Bilinear, clamped at the edges, at texture coordinates
					float Sample(float u, float v) const;
				};

				// Opacity micromaps (VK_EXT_opacity_micromap) for alpha tested triangles.
				//
				// Each triangle is split into 4^level micro-triangles, and each micro-triangle is marked as opaque,
				// transparent, or unknown (it crosses the edge of the alpha cutout). Rays then skip transparent parts
				// and stop on opaque parts in hardware; only unknown micro-triangles run the any-hit shader's alpha
				// test. Micromaps live in barycentric space, so they stay valid when the mesh deforms.
				//
				// Triangles with the same texture coordinates share one micromap entry: thousands of leaf cards
				// built from one layout need only that layout's entries. Triangles that are opaque or transparent
				// throughout get a special index and no entry at all.
				class OpacityMicromapBuilder {
				public:
					OpacityMicromapBuilder() = default;
					~OpacityMicromapBuilder();

					OpacityMicromapBuilder(const OpacityMicromapBuilder&) = delete;
					OpacityMicromapBuilder& operator=(const OpacityMicromapBuilder&) = delete;

					static void RegisterExtensionFunctions(VkDevice device);

					// The micromap index for one triangle: an entry of this micromap, or one of the special indices
					// VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_OPAQUE_EXT / _FULLY_TRANSPARENT_EXT. uv0..uv2 in the
					// triangle's vertex order. A point is opaque where alphaScale * mask >= cutoff.
					int32_t AddTriangle(const AlphaMask& mask, float alphaScale, float cutoff, glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2);

					// Subdivision level of an entry
					uint32_t LevelOf(int32_t index) const { return index >= 0 ? triangles[index].subdivisionLevel : 0; }

					size_t EntryCount() const { return triangles.size(); }
					size_t DataSize() const { return data.size(); }

					// Uploads and builds the micromap on the GPU (waits for it). No entries: nothing is built.
					void Build(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue);

					VkMicromapEXT Get() const { return micromap; }

					// Index -> barycentric order of micro-triangles, from the Vulkan specification
					static uint32_t BarycentricsToSpaceFillingCurveIndex(float u, float v, uint32_t level);

				private:
					std::vector<uint8_t> data;                     // 2 bits per micro-triangle
					std::vector<VkMicromapTriangleEXT> triangles;  // One per entry
					std::map<std::array<int64_t, 8>, int32_t> cache; // Same texture coordinates, mask and cutoff

					// Classified triangles, kept across builders: rebuilding the micromaps of meshes that changed
					// only copies what an earlier build already worked out (the classification is the slow part)
					struct Classified {
						int32_t special = 0;        // A special index, or 0 when the triangle has data
						uint32_t level = 0;
						std::vector<uint8_t> data;
					};
					static std::map<std::array<int64_t, 8>, Classified> classified;
					static Classified Classify(const AlphaMask& mask, float alphaScale, float cutoff, glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2);

					VkDevice device = VK_NULL_HANDLE;
					VkMicromapEXT micromap = VK_NULL_HANDLE;
					std::unique_ptr<Buffer> storage;

					static PFN_vkCreateMicromapEXT vkCreateMicromapEXT;
					static PFN_vkDestroyMicromapEXT vkDestroyMicromapEXT;
					static PFN_vkCmdBuildMicromapsEXT vkCmdBuildMicromapsEXT;
					static PFN_vkGetMicromapBuildSizesEXT vkGetMicromapBuildSizesEXT;
				};
			}
		}
	}
}
