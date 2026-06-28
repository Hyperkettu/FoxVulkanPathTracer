#include "FoxRenderer.h"

namespace Fox {

	namespace Scene {
	
		namespace RayTracing {
		
			void MainRayTracingScene::Initialize() {
			
				//mesh = Fox::Graphics::Geometry::GeometryGenerator::GeneratePlaneMesh(1, 1, 1.0f, 1.0f, 0.0f);

				mesh = Fox::Graphics::Geometry::Vulkan::Mesh();
				mesh.Load("Models/cornell_box.glb");
	
				AddMesh(mesh, glm::mat4(1.0f));

				glm::mat4 offset = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f));

		//		AddMesh(mesh, offset);

				Build();
			
			}

			void MainRayTracingScene::Update(float dt) {


			}
		
		}
	
	}

}