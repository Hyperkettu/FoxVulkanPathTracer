#pragma once

#include "FoxVulkanPathTracer/Core/Loaders/GLTFLoader.h"

namespace Fox {

	namespace Graphics {

		namespace Geometry {
			
			namespace Vulkan {

				class Mesh
				{
				public:
					using IndexType = uint32_t;

					Mesh() = default;

					Mesh(
						VkDevice device, 
						VkPhysicalDevice physicalDevice, 
						VkQueue graphicsQueue,
						uint32_t queueFamilyIndex
					) : device(device), 
						physicalDevice(physicalDevice), 
					    graphicsQueue(graphicsQueue) 
					{
						commandPool = std::make_unique<Fox::Graphics::Vulkan::CommandPool>(device, queueFamilyIndex);
					} 

					Mesh(std::vector<Fox::Graphics::Vulkan::Vertex>& vertices, std::vector<IndexType>& indices)
						: vertices(vertices), indices(indices) {
					} 

					void LoadFromGLTF(const std::string& filePath);


					void AddTriangle(const Fox::Graphics::Vulkan::Vertex& v0, const Fox::Graphics::Vulkan::Vertex& v1, const Fox::Graphics::Vulkan::Vertex& v2)
					{
						// Try to find current submesh
						if (submeshes.empty() || submeshes.back().IsFull())
						{
							StartNewSubmesh();
						}

						Fox::Graphics::Geometry::Submesh& submesh = submeshes.back();

						// Map unique vertices to indices (avoid duplicates if desired)
						uint32_t i0 = AddVertex(v0);
						uint32_t i1 = AddVertex(v1);
						uint32_t i2 = AddVertex(v2);

						indices.push_back(i0);
						indices.push_back(i1);
						indices.push_back(i2);

						submesh.AddTriangle(i0, i1, i2);
					}

					void AddTriangle(IndexType i0, IndexType i1, IndexType i2, bool startNewSubmesh)
					{
						// Try to find current submesh
						if (submeshes.empty() || submeshes.back().IsFull()) 
						{
							StartNewSubmesh();
						}

						Fox::Graphics::Geometry::Submesh& submesh = submeshes.back();

						indices.push_back(i0);
						indices.push_back(i1);
						indices.push_back(i2);

						submesh.AddTriangle(i0, i1, i2);

						if (startNewSubmesh) {
							StartNewSubmesh();
						}
					}

					uint32_t AddVertex(const Fox::Graphics::Vulkan::Vertex& v)
					{
						if (submeshes.empty() || submeshes.back().IsFull())
						{
							StartNewSubmesh();
						}

						uint32_t index = static_cast<uint32_t>(vertices.size());
						vertices.push_back(v);
						submeshes.back().vertexCount++;
						uint32_t numVertices = GetVertexCountInSubmeshesBeforeLatest();
						return (index - numVertices);
					}


					uint32_t GetVertexCountInSubmeshesBeforeLatest() const {
						uint32_t vertexCount = 0;
						for (auto i = 0; i < submeshes.size() - 1; i++) {
							vertexCount += submeshes[i].vertexCount;
						}
						return vertexCount;
					}

					std::vector<Fox::Graphics::Vulkan::MeshInfo> GetMeshInfos() const {
						std::vector<Fox::Graphics::Vulkan::MeshInfo> infos;

						for (auto& submesh : submeshes) {
							Fox::Graphics::Vulkan::MeshInfo info;
							info.vertexOffset = submesh.vertexOffset;
							info.modelIndex = 0;
							info.indexOffset = submesh.indexOffset;
							info.indexCount = submesh.indexCount;
							infos.push_back(info);
						}

						return infos;
					}

					const std::vector<Fox::Graphics::Vulkan::Vertex>& GetVertices() const { return vertices; }
					const std::vector<IndexType>& GetIndices() const { return indices; }
					const std::vector<Fox::Graphics::Geometry::Submesh>& GetSubmeshes() const { return submeshes; }
					const std::vector<Fox::Graphics::Vulkan::Submesh>& GetSubmeshesForGPU() const { return gpuSubmeshes; }
					const std::vector<Fox::Graphics::Vulkan::Material>& GetMaterials() const { return materials;  }
					const std::vector<Fox::Graphics::Vulkan::Light>& GetLights() const { return lights; }


					std::vector<Fox::Graphics::Vulkan::RayTracing::RayTracingInstance> GetInstanceData() {
						return instances;
					}

				private:
					VkDevice device;  
					VkPhysicalDevice physicalDevice;
					VkQueue graphicsQueue;

					std::unique_ptr<Fox::Graphics::Vulkan::CommandPool> commandPool; 


					std::vector<Fox::Graphics::Vulkan::Vertex> vertices;
					std::vector<IndexType> indices;
					std::vector<Fox::Graphics::Geometry::Submesh> submeshes;
					std::vector<Fox::Graphics::Vulkan::RayTracing::RayTracingInstance> instances;
					std::vector<Fox::Graphics::Vulkan::Submesh> gpuSubmeshes;
					std::vector<Fox::Graphics::Vulkan::Material> materials;
					std::vector<Fox::Graphics::Vulkan::Light> lights;

					std::unordered_map<size_t, uint32_t> vertexLookup;

					void StartNewSubmesh()
					{
						Fox::Graphics::Geometry::Submesh sub{};
						sub.vertexOffset = static_cast<uint32_t>(vertices.size());
						sub.indexOffset = static_cast<uint32_t>(indices.size());
						submeshes.push_back(sub);
					}
				};
			}
		}
	}
}