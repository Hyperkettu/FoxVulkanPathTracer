#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			class Buffer;

			namespace RayTracing {

				// The hybrid path tracer's first hits: rasterized, Nanite style, instead of traced from the camera.
				//
				// The scene is cut into clusters of up to 128 triangles, each with a bounding sphere (RayTracingScene::
				// MakeRasterContent). Every frame, on the GPU:
				//
				//   1. cull, phase 1   per instance, then per cluster: outside the view frustum? hidden behind the previous
				//                      frame's depth pyramid (where it was then)? The rest become indirect draws; the hidden
				//                      ones become candidates. Trees drawn through cluster level of detail use the clusters
				//                      their level of detail selected this frame (ClusterLodPass), as the rays do.
				//   2. draw            the visibility buffer: per pixel, which draw and which triangle (and depth)
				//   3. depth pyramid   of what was drawn
				//   4. cull, phase 2   the candidates against it: whatever came into view
				//   5. draw            those too; then the final depth pyramid, for the next frame and the screen traces
				//
				// The path tracer (raygen.slang) reads the visibility buffer for the surface each pixel sees, and the depth
				// pyramid for its screen traces. Draws are not indexed: the vertex shader reads the scene's index and vertex
				// buffers (the very data the rays hit), so a pixel's triangle is the one a camera ray would find.
				class HybridRasterPass {
				public:
					static constexpr uint32_t CHUNK_TRIANGLES = 128;
					static constexpr uint32_t CHUNK_MASKED = 1;     // Alpha tested
					static constexpr uint32_t CHUNK_UNBOUNDED = 2;  // Vertices move: not culled on its own
					static constexpr uint32_t RASTER_CLUSTER_LOD = 1;
					static constexpr uint32_t LIST_COUNT = 4;       // Phases 1 and 2, opaque and alpha tested
					// Visible clusters go into buckets by size (up to 32, 64, 128, 256 triangles): one instanced draw each
					static constexpr uint32_t BUCKETS_PER_LIST = 4;
					static constexpr uint32_t BUCKETS = LIST_COUNT * BUCKETS_PER_LIST;
					static constexpr uint32_t BUCKET_SIZES[BUCKETS_PER_LIST] = { 32, 64, 128, 256 };
					static constexpr uint32_t BUCKET_CAPACITY = 1u << 18; // Clusters a bucket holds
					// Small clusters (their triangles a pixel or so) are rasterized in software (hybrid_swraster.slang), a list
					// per phase after the buckets; its dispatch follows the draws in the draw buffer
					static constexpr uint32_t SOFTWARE_DISPATCH = 64; // uints
					static constexpr uint32_t MAX_CANDIDATES = 1u << 19;
					static constexpr uint32_t MAX_INSTANCES = 131'072;
					static constexpr uint32_t COUNTERS = 32;

					// Must match Chunk in Shaders/hybrid_common.slang
					struct Chunk {
						glm::vec4 sphere{ 0.0f };   // Object space centre, radius
						uint32_t submesh = 0;
						uint32_t firstTriangle = 0;
						uint32_t triangleCount = 0;
						uint32_t flags = 0;
					};
					// Must match RasterInstance in Shaders/hybrid_common.slang; one per top level instance
					struct RasterInstance {
						glm::vec4 sphere{ 0.0f, 0.0f, 0.0f, -1.0f }; // Around its clusters; radius < 0: unbounded
						uint32_t firstChunk = 0;
						uint32_t chunkCount = 0;
						uint32_t flags = 0;
						uint32_t pad = 0;
					};
					// Must match DrawInfo in Shaders/hybrid_common.slang
					struct DrawInfo {
						uint32_t tlasInstance, submesh, firstIndex, triangleCount;
					};
					// Must match CullConstants in Shaders/hybrid_common.slang
					struct CullConstants {
						glm::vec4 viewProj[4];
						glm::vec4 prevViewProj[4];
						glm::vec4 planes[6];
						glm::vec4 cameraPosition;
						glm::vec4 hiz;
						glm::uvec4 counts;
						glm::uvec4 limits;
						glm::vec4 lod;
					};

					struct Content {
						std::vector<Chunk> chunks;
						std::vector<RasterInstance> instances; // By top level instance
						uint32_t clusterChunkBase = 0;         // Chunk of global LOD cluster 0
					};

					// The camera, as the path tracer's rays see it: x right, y up, z forward; projection rows (clip =
					// dot(row, (p, 1))) with reversed depth (near 1, infinitely far 0)
					struct View {
						glm::vec3 position{ 0.0f }, forward{ 0.0f, 0.0f, 1.0f }, right{ 1.0f, 0.0f, 0.0f }, up{ 0.0f, 1.0f, 0.0f };
						float tanHalfFov = 0.5f;
						float aspect = 1.0f;
						float nearPlane = 0.05f;
						glm::vec2 jitter{ 0.0f }; // Pixels, as the primary rays' (camera.jitter)
					};

					struct FrameInput {
						VkBuffer tlasInstances = VK_NULL_HANDLE; // VkAccelerationStructureInstanceKHR, this frame's
						bool tlasInstancesWritten = false;       // Written this frame (else last copied ones still hold)
						uint32_t instanceCount = 0;
						VkBuffer instanceMotion = VK_NULL_HANDLE;
						VkBuffer submeshes = VK_NULL_HANDLE, vertices = VK_NULL_HANDLE, indices = VK_NULL_HANDLE, materials = VK_NULL_HANDLE;
						// Trees through cluster level of detail (ClusterLodPass's buffers; instanceCount 0: none)
						VkBuffer clusterInstances = VK_NULL_HANDLE, clusterCounts = VK_NULL_HANDLE, clusterSelected = VK_NULL_HANDLE;
						uint32_t clusterInstanceCount = 0;
						VkBuffer lodClusters = VK_NULL_HANDLE; // The trees' cluster graph (their own raster cut, Nanite style)
						float lodPixelScale = 0.0f;            // Pixels per world unit at distance 1 at the raster's resolution (0: the rays' cut)
						float lodThresholdScale = 1.0f;
						float softwareLimit = 32.0f;           // Clusters narrower than this many pixels: software raster (0: none)
						View view;
						bool resetHistory = false;
						bool occlusionCulling = true;
					};

					struct Stats {
						uint32_t drawsPhase1 = 0, drawsPhase2 = 0;
						uint32_t clustersTested = 0, clustersOutside = 0, clustersOccluded = 0, clustersRevealed = 0;
						uint32_t instancesVisible = 0;
						uint32_t triangles = 0;
						uint32_t overflow = 0;
						uint32_t chunks = 0;
						uint32_t listDraws[4] = {}, listTriangles[4] = {}; // Phase 1 opaque, alpha tested; phase 2 the same
						uint32_t softwareTriangles = 0;
						VkDeviceSize memory = 0;
					};

					HybridRasterPass(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue,
						VkDescriptorSetLayout bindlessLayout, uint32_t framesInFlight);
					~HybridRasterPass();
					HybridRasterPass(const HybridRasterPass&) = delete;
					HybridRasterPass& operator=(const HybridRasterPass&) = delete;

					// New clusters (device idle)
					void Upload(const Content& content);
					// Images at the path tracer's resolution (device idle)
					void Resize(VkExtent2D extent);

					// Records the passes; mark: GPU profiler marks
					void Record(VkCommandBuffer cmd, uint32_t frameSlot, const FrameInput& input, VkDescriptorSet bindlessSet,
						const std::function<void(const char*)>& mark = {});
					void ReadStats(uint32_t frameSlot);
					const Stats& GetStats() const { return stats; }

					// What the path tracer reads (descriptor set 0 of the ray tracing pipeline)
					VkImageView GetVisibilityView() const { return visibility.view; }
					VkImageView GetDepthPyramidView() const { return pyramid.view; }
					VkSampler GetPointSampler() const { return pointSampler; }
					VkSampler GetLinearSampler() const { return linearSampler; }
					VkBuffer GetDrawInfos() const;
					VkBuffer GetInstances() const;
					VkExtent2D GetExtent() const { return extent; }

					// The projection rows of a view (also what the path tracer's screen traces need)
					static void ViewProjection(const View& view, VkExtent2D extent, bool jittered, glm::vec4 rows[4]);

				private:
					struct Image {
						VkImage image = VK_NULL_HANDLE;
						VkDeviceMemory memory = VK_NULL_HANDLE;
						VkImageView view = VK_NULL_HANDLE;           // All levels
						std::vector<VkImageView> levels;             // One per level (storage)
						uint32_t levelCount = 1;
					};
					struct Frame {
						std::unique_ptr<Buffer> constants;  // CullConstants, host visible
						std::unique_ptr<Buffer> stats;      // Counters copied after the passes, host visible
						VkDescriptorSet cullSet = VK_NULL_HANDLE;
						bool statsPending = false;
					};

					std::unique_ptr<Buffer> MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible = false);
					void UploadTo(Buffer& buffer, const void* data, VkDeviceSize size);
					Image MakeImage(VkExtent2D size, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t levels);
					void DestroyImage(Image& image);
					VkShaderModule LoadShader(const char* path);
					void CreatePipelines(VkDescriptorSetLayout bindlessLayout);
					void WriteCullSet(Frame& frame, const FrameInput& input);
					void WriteRasterSet(const FrameInput& input);
					void BuildPyramid(VkCommandBuffer cmd);
					void Draw(VkCommandBuffer cmd, uint32_t phase, bool clear, const glm::vec4 viewProj[4], VkDescriptorSet bindlessSet);
					void RasterizeSoftware(VkCommandBuffer cmd, uint32_t phase, const glm::vec4 viewProj[4], VkDescriptorSet bindlessSet);
					void Resolve(VkCommandBuffer cmd);
					void Cull(VkCommandBuffer cmd, VkPipeline pipeline, uint32_t groups, VkDescriptorSet set);

					VkDevice device;
					VkPhysicalDevice physicalDevice;
					VkCommandPool commandPool;
					VkQueue queue;

					VkDescriptorPool pool = VK_NULL_HANDLE;
					VkDescriptorSetLayout cullLayout = VK_NULL_HANDLE, rasterLayout = VK_NULL_HANDLE, pyramidLayout = VK_NULL_HANDLE;
					VkPipelineLayout cullPipelineLayout = VK_NULL_HANDLE, rasterPipelineLayout = VK_NULL_HANDLE, pyramidPipelineLayout = VK_NULL_HANDLE;
					VkPipeline cullPipelines[4] = {}; // Instances, trees, candidates, instances with many clusters
					VkPipeline rasterPipelines[2] = {}; // Opaque, alpha tested
					VkPipeline pyramidPipeline = VK_NULL_HANDLE;
					VkDescriptorSetLayout softwareLayout = VK_NULL_HANDLE, resolveLayout = VK_NULL_HANDLE;
					VkPipelineLayout softwarePipelineLayout = VK_NULL_HANDLE, resolvePipelineLayout = VK_NULL_HANDLE;
					VkPipeline softwarePipeline = VK_NULL_HANDLE, resolvePipeline = VK_NULL_HANDLE;
					VkDescriptorSet softwareSet = VK_NULL_HANDLE, resolveSet = VK_NULL_HANDLE;
					std::unique_ptr<Buffer> software; // Per pixel: depth << 32 | record << 8 | triangle (64-bit atomic max)
					VkBuffer softwareBound = VK_NULL_HANDLE;
					VkSampler pointSampler = VK_NULL_HANDLE, linearSampler = VK_NULL_HANDLE;
					VkDescriptorSet rasterSet = VK_NULL_HANDLE;
					std::vector<VkDescriptorSet> pyramidSets; // Level 0 from the depth buffer, then each from the one below

					std::unique_ptr<Buffer> chunks, rasterInstances, tlasCopy, draws, drawInfos, counters, candidates, dummy, largeInstances;
					static constexpr uint32_t LARGE_CAPACITY = 16384; // Instances with many clusters (hybrid_cull.slang)
					uint32_t chunkCount = 0, clusterChunkBase = 0;
					std::vector<Frame> frames;

					VkExtent2D extent{ 0, 0 };
					Image visibility, depth, pyramid;
					bool pyramidValid = false;     // The previous frame's depth pyramid can be culled against
					glm::vec4 previousViewProj[4] = {};
					VkBuffer boundTlasSource = VK_NULL_HANDLE;
					VkBuffer rasterBound[4] = {};
					Stats stats;
					VkDeviceSize memory = 0;
				};
			}
		}
	}
}
