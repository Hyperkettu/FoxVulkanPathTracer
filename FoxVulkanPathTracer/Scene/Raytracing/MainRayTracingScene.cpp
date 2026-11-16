#include "FoxRenderer.h"

namespace Fox {

	namespace Scene {

		namespace RayTracing {

			namespace {
				// The cabin model's foundation ends at this height; the ground is placed there
				const float GROUND_HEIGHT = -3.73f;

				// The sun of Textures/environment_map.hdr sits on the horizon in this direction.
				// The analytic sun is raised a little for longer-reaching, less grazing light.
				const glm::vec3 SUN_DIRECTION(0.876f, 0.22f, 0.479f);

				// Golden hour: warm, and not much stronger than the sky itself
				const glm::vec3 SUN_IRRADIANCE = glm::vec3(1.0f, 0.58f, 0.30f) * 18.0f;
				const float SUN_ANGULAR_RADIUS = 1.2f; // Degrees. Larger than the real sun for softer shadows
			}

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

				// The box model's floor is at y = -0.55: lower it so it rests on the ground
				glm::mat4 boxTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, GROUND_HEIGHT + 0.55f, 0.0f));
				boxTransform = glm::rotate(boxTransform, glm::radians(25.0f), up);
				AddMesh(box, boxTransform);

				// Second cabin: the same mesh instanced with another transform
				glm::mat4 secondCabinTransform = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), up);
				secondCabinTransform = glm::translate(secondCabinTransform, glm::vec3(50.0f, 0.0f, 50.0f));
				AddMesh(cabin, secondCabinTransform);

				// Dark, slightly glossy ground (think wet asphalt): picks up reflections of the sky and the buildings
				Fox::Graphics::Vulkan::Material ground;
				ground.baseColor[0] = 0.07f;
				ground.baseColor[1] = 0.07f;
				ground.baseColor[2] = 0.08f;
				ground.roughness = 0.12f;
				ground.metallic = 0.0f;
				AddGroundPlane(GROUND_HEIGHT, 2000.0f, ground);

				AddSun(SUN_DIRECTION, SUN_IRRADIANCE, SUN_ANGULAR_RADIUS);

				Build();
			}

			void MainRayTracingScene::Update(float dt) {
			}
		}
	}
}
