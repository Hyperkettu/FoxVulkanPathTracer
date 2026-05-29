#include "FoxRenderer.h"

// Static member definitions
std::unique_ptr<Fox::Graphics::Managers::Vulkan::SceneManager> Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::SceneManager>::instance = nullptr;
std::once_flag Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::SceneManager>::initFlag;

namespace Fox {

	namespace Graphics {

		namespace Managers {

			namespace Vulkan {

				bool SceneManager::Initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkSurfaceCapabilitiesKHR capabilities) {
					
					auto scene = std::make_unique<Fox::Scene::MainScene>();
					scene->Initialize(device, physicalDevice, commandPool, queue, capabilities);
					scenes[Fox::Graphics::Managers::Vulkan::SceneId::MAIN_SCENE] = std::move(scene);
					

				//	Fox::Graphics::Geometry::GeometryGenerator::GenerateCube(verticesCube, indicesCube, meshInfoCube);


					return true;
				}

			}
		}
	}
}