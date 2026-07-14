#include "FoxRenderer.h"

namespace Fox {

	namespace Scene {
	
		namespace RayTracing {
		
			void MainRayTracingScene::Initialize() {
				auto mesh = std::make_unique<Fox::Graphics::Geometry::Vulkan::Mesh>(device, physicalDevice, queue, queueFamily); 
				mesh->LoadFromGLTF("Models/cabin.glb");

				glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(20.0f, 0.0f, 0.0f));
				model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	
				AddMesh(mesh, model, false);

				auto mesh2 = std::make_unique<Fox::Graphics::Geometry::Vulkan::Mesh>(device, physicalDevice, queue, queueFamily);
				mesh2->LoadFromGLTF("Models/box.glb");
				
				glm::mat4 offset = glm::rotate(glm::mat4(1.0f), glm::radians(25.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			//    offset = glm::translate(offset, glm::vec3(50.0f, 0.0f, 0.0f));
				AddMesh(mesh2, offset, false);

				auto mesh3 = std::make_unique<Fox::Graphics::Geometry::Vulkan::Mesh>(device, physicalDevice, queue, queueFamily);
				mesh3->LoadFromGLTF("Models/cabin.glb");


				glm::mat4 offset2 = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				    offset2 = glm::translate(offset2, glm::vec3(50.0f, 0.0f, 50.0f));
				AddMesh(mesh3, offset2, true);


				Build();
			
			}

			void MainRayTracingScene::Update(float dt) {


			}
		
		}
	
	}

}