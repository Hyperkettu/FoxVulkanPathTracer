#include "FoxRenderer.h"

namespace Fox {

	namespace Scene {
	
		namespace RayTracing {
		
			void MainRayTracingScene::Initialize() {
			
				//mesh = Fox::Graphics::Geometry::GeometryGenerator::GeneratePlaneMesh(1, 1, 1.0f, 1.0f, 0.0f);

				mesh = Fox::Graphics::Geometry::Vulkan::Mesh();
				mesh.Load("Models/cornell_box.glb");

				/*for (auto& submesh : mesh.GetSubmeshes()) {
					std::vector<Fox::Graphics::Vulkan::Vertex> vertices;
					for (auto i = 0; i < submesh.vertexCount; i++) {
						vertices.push_back(meshVertices[i + submesh.vertexOffset]);
					}
					allMeshVertices.push_back(vertices);

					std::vector<uint32_t> indices;
					for (auto i = 0; i < submesh.indexCount; i++) {
						uint32_t localIndex = meshIndices[submesh.indexOffset + i] - submesh.vertexOffset;
						indices.push_back(localIndex);
					}
					allMeshIndices.push_back(indices);
				}*/

			 //  UploadSceneGeometry(device, physicalDevice, allMeshVertices, allMeshIndices);

		//		mesh = Fox::Graphics::Geometry::GeometryGenerator::GeneratePlaneMesh(1, 1, 1.0f, 1.0f, 0.0f);

//				mesh = Fox::Graphics::Geometry::GeometryGenerator::GenerateCube();	
	
				AddMesh(mesh, glm::mat4(1.0f));

				glm::mat4 offset = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f));

		//		AddMesh(mesh, offset);

				Build();
			
			}

			void MainRayTracingScene::Update(float dt) {


			}
		
		}
	
	}

}