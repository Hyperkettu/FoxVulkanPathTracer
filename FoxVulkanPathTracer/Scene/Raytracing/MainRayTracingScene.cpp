#include "FoxRenderer.h"

namespace Fox {

	namespace Scene {

		namespace RayTracing {

			void MainRayTracingScene::Initialize() {
				using Fox::Graphics::Geometry::Vulkan::Mesh;

				const glm::vec3 up(0.0f, 1.0f, 0.0f);

				// The meshes are only needed while the scene is assembled: AddMesh copies their
				// geometry, and the textures are owned by the TextureManager.
				Mesh cabin(device, physicalDevice, queue, queueFamily);
				cabin.LoadFromGLTF("Models/cabin.glb");

				Mesh box(device, physicalDevice, queue, queueFamily);
				box.LoadFromGLTF("Models/box.glb");

				glm::mat4 cabinTransform = glm::translate(glm::mat4(1.0f), glm::vec3(20.0f, 0.0f, 0.0f));
				cabinTransform = glm::rotate(cabinTransform, glm::radians(90.0f), up);
				AddMesh(cabin, cabinTransform);

				AddMesh(box, glm::rotate(glm::mat4(1.0f), glm::radians(25.0f), up));

				// Second cabin: the same mesh instanced with another transform
				glm::mat4 secondCabinTransform = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), up);
				secondCabinTransform = glm::translate(secondCabinTransform, glm::vec3(50.0f, 0.0f, 50.0f));
				AddMesh(cabin, secondCabinTransform);

				Build();
			}

			void MainRayTracingScene::Update(float dt) {
			}
		}
	}
}
