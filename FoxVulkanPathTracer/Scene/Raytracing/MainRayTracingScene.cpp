#include "FoxRenderer.h"

namespace Fox {

	namespace Scene {
	
		namespace RayTracing {
		
			void MainRayTracingScene::Initialize() {
				mesh = std::make_unique<Fox::Graphics::Geometry::Vulkan::Mesh>(device, physicalDevice, queue, queueFamily); 
				mesh->LoadFromGLTF("Models/cabin.glb");
	
				AddMesh(mesh, glm::mat4(1.0f));

		//		glm::mat4 offset = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f));
		//		AddMesh(mesh, offset);

				Build();
			
			}

			void MainRayTracingScene::Update(float dt) {


			}
		
		}
	
	}

}