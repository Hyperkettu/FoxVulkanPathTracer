#include "FoxRenderer.h"

namespace Fox {

    namespace Scene {

        void MainScene::Initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkSurfaceCapabilitiesKHR capabilities) {
			SetUpCamera(capabilities);
			AddMeshToScene(Fox::Graphics::Managers::Vulkan::MeshResource::SUBDIVIDED_PLANE);

		//	BuildBuffers(device, physicalDevice, commandPool, queue);
        }

        void MainScene::Update(float deltaTime) {}

        void MainScene::SetUpCamera(VkSurfaceCapabilitiesKHR capabilities) {
			mainCamera = std::make_unique<Fox::Graphics::Vulkan::Camera>(60.0f, static_cast<float>(capabilities.currentExtent.width) / static_cast<float>(capabilities.currentExtent.height), 0.1f, 10000.0f);
			// Low on the sunlit side, looking back at the box and the first cabin (see MainRayTracingScene.cpp)
			mainCamera->SetPosition(glm::vec3(29.0f, -2.6f, 17.0f));
			mainCamera->SetCameraTarget(glm::vec3(4.0f, -0.6f, -3.0f));
			mainCamera->SetWorldUp(glm::vec3(0.0f, 1.0f, 0.0f));
        }

		void MainScene::AddMeshToScene(Fox::Graphics::Managers::Vulkan::MeshResource id) { 
		 /*	auto vertices = Fox::Graphics::Managers::Vulkan::MeshManager::Get().GetMesh(Fox::Graphics::Managers::Vulkan::MeshResource::SUBDIVIDED_PLANE)->GetVertices();
			sceneVertices.insert(sceneVertices.end(), vertices.begin(), vertices.end());

			auto indices  = Fox::Graphics::Managers::Vulkan::MeshManager::Get().GetMesh(Fox::Graphics::Managers::Vulkan::MeshResource::SUBDIVIDED_PLANE)->GetIndices();
			sceneIndices.insert(sceneIndices.end(), indices.begin(), indices.end());

			auto meshInfos = Fox::Graphics::Managers::Vulkan::MeshManager::Get().GetMesh(Fox::Graphics::Managers::Vulkan::MeshResource::SUBDIVIDED_PLANE)->GetMeshInfos();
			sceneMeshInfos.insert(sceneMeshInfos.end(), meshInfos.begin(), meshInfos.end());*/
		}

    }
}