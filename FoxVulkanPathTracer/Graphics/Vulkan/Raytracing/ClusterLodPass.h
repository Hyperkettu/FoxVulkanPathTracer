#pragma once

#include <memory>
#include <vector>

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			class DescriptorSetBuilder;
			class DescriptorSet;
			class PipelineLayout;

			namespace RayTracing {

				// Traces meshes with cluster level of detail (Graphics/Geometry/ClusterLod.h) through cluster acceleration
				// structures (VK_NV_cluster_acceleration_structure). Each cluster is built once as a template (its
				// triangles, in the bind pose). Every frame (Record):
				//
				//   1. select   each instance picks its clusters by projected error (Shaders/cluster_lod.slang)
				//   2. prepare  every cluster some instance picked gets its vertices from the skinned mesh
				//   3. build    a cluster acceleration structure for each of them, instantiated from its template with
				//               the vertices where the wind put them this frame (much faster than building from triangles)
				//   4. refs     each instance lists the structures of its clusters
				//   5. build    a bottom level structure per instance from its list (a fast build: no triangles)
				//   6. patch    each instance's top level entry points at its new structure
				//
				// The top level structure is then built as usual. Clusters are shared between instances: a forest of
				// instances of one tree builds each of its clusters once, whatever the number of instances using it.
				class ClusterLodPass {
				public:
					// Must match Cluster in Shaders/cluster_lod.slang
					struct GpuCluster {
						glm::vec4 selfBounds;
						glm::vec4 parentBounds;
						float selfError;
						float parentError;
						uint32_t gatherFirst;
						uint32_t gatherCount;
					};

					// Must match Instance in Shaders/cluster_lod.slang
					struct Instance {
						glm::vec4 rows[3];
						float scale;
						uint32_t firstCandidate;
						uint32_t candidateCount;
						uint32_t refBase;
						uint32_t workBase;
						uint32_t tlasInstance;
						float threshold;
						uint32_t pad;
					};

					// The clusters of every mesh, as RayTracingScene collects them
					struct Content {
						std::vector<GpuCluster> clusters;
						std::vector<VkClusterAccelerationStructureBuildTriangleClusterInfoNV> templates; // Addresses as offsets (see Upload)
						std::vector<uint32_t> localVertexIds;    // Scene vertex index per cluster vertex
						std::vector<float> bindPositions;        // 3 per cluster vertex: the bind pose (the templates' shape)
						std::vector<uint8_t> localIndices;       // 3 per triangle, into the cluster's vertices
						uint32_t maxClustersPerMesh = 0;
						uint32_t maxTrianglesPerCluster = 0;
						uint32_t maxVerticesPerCluster = 0;
						uint32_t totalTriangles = 0;
						uint32_t instanceCount = 0;              // Placements traced through clusters
					};

					struct View {
						glm::vec3 cameraPosition{ 0.0f };
						float pixelScale = 1000.0f; // Image height / (2 tan(vertical fov / 2))
					};

					// Last statistics read back (a frame or two old)
					struct Stats {
						uint32_t clustersBuilt = 0;    // Distinct clusters built this frame
						uint32_t clustersSelected = 0; // Over all instances
						uint32_t instances = 0;
						uint32_t candidates = 0;
						VkDeviceSize memory = 0;       // Bytes of the pass's buffers
					};

					ClusterLodPass(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue);
					~ClusterLodPass();

					ClusterLodPass(const ClusterLodPass&) = delete;
					ClusterLodPass& operator=(const ClusterLodPass&) = delete;

					static void RegisterExtensionFunctions(VkDevice device);

					// New content: uploads it and sizes every buffer. The templates' index and vertex buffer addresses are
					// byte offsets into localIndices and the gathered positions; Upload makes them addresses. Device idle.
					void Upload(const Content& content, VkBuffer sceneVertices);

					// Records this frame's passes and builds. instances: this frame's, with candidate ranges and work and
					// ref bases filled in (workCount: their total candidates). tlasInstances: the instance buffer the top
					// level build will read (host visible, already written; the pass patches its addresses).
					void Record(VkCommandBuffer cmd, uint32_t frameSlot, const std::vector<Instance>& instances, uint32_t workCount,
						VkBuffer tlasInstances, const View& view, const std::function<void(const char*)>& mark = {});

					// Reads the statistics the frame slot's last use left (call after its fence was waited on)
					void ReadStats(uint32_t frameSlot);
					const Stats& GetStats() const { return stats; }

					uint32_t GetRefCapacity() const { return refCapacity; }
					uint32_t GetBottomLevelBudget() const { return bottomLevelBudget; }

				private:
					struct Pipeline {
						VkPipeline pipeline = VK_NULL_HANDLE;
					};

					std::unique_ptr<Buffer> MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible = false);
					void UploadTo(Buffer& buffer, const void* data, VkDeviceSize size);
					void BindFrameSet(uint32_t frameSlot, VkBuffer tlasInstances);
					void Barrier(VkCommandBuffer cmd) const;

					VkDevice device = VK_NULL_HANDLE;
					VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
					VkCommandPool commandPool = VK_NULL_HANDLE;
					VkQueue queue = VK_NULL_HANDLE;
					VkPhysicalDeviceClusterAccelerationStructurePropertiesNV properties{};

					std::unique_ptr<DescriptorSetBuilder> descriptors;
					std::unique_ptr<PipelineLayout> layout;
					VkPipeline pipelines[4] = {};

					// Content
					uint32_t clusterCount = 0;
					uint32_t instanceCapacity = 0;
					uint32_t refCapacity = 0;
					uint32_t bottomLevelBudget = 0;
					VkBuffer sceneVertices = VK_NULL_HANDLE;
					std::unique_ptr<Buffer> clusters, templates, localVertexIds, localIndices, positions;
					std::unique_ptr<Buffer> usedStamp, clusterSlot, counters, buildInfos, clusterAddresses, clusterData;
					std::unique_ptr<Buffer> templateData, templateAddresses; // Built once per upload
					std::unique_ptr<Buffer> selected, refs, instanceCounts, bottomLevelInfos, bottomLevelAddresses, bottomLevelData, scratch;
					VkClusterAccelerationStructureTriangleClusterInputNV triangleInput{};
					VkClusterAccelerationStructureClustersBottomLevelInputNV bottomLevelInput{};

					// Per frame slot
					struct Frame {
						std::unique_ptr<Buffer> instances;   // Host visible
						std::unique_ptr<Buffer> stats;       // Host visible: counters copied after the passes
						std::unique_ptr<DescriptorSet> set;
						VkBuffer boundTlas = VK_NULL_HANDLE;
						uint32_t boundVersion = 0;
						bool statsPending = false;
						uint32_t instanceCount = 0;
						uint32_t candidates = 0;
					};
					std::vector<Frame> frames;
					uint32_t contentVersion = 0;
					uint32_t frameStamp = 0;
					VkDeviceSize memory = 0;
					Stats stats;

					static PFN_vkGetClusterAccelerationStructureBuildSizesNV vkGetClusterAccelerationStructureBuildSizesNV;
					static PFN_vkCmdBuildClusterAccelerationStructureIndirectNV vkCmdBuildClusterAccelerationStructureIndirectNV;
				};
			}
		}
	}
}
