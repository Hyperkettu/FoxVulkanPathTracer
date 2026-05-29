#pragma once

#include <unordered_map>

namespace Fox {

	namespace Graphics {

		namespace Managers {

			namespace Vulkan {

				enum class SceneId : int32_t {
					MAIN_SCENE = 0
				};

				class SceneManager : public Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::SceneManager> {  
					friend class Singleton <Fox::Graphics::Managers::Vulkan::SceneManager>;

				public: 

					SceneManager() = default;
					~SceneManager() = default;

					bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkSurfaceCapabilitiesKHR capabiliies);

					void Destroy() {
						scenes.clear();
					}

					inline std::unique_ptr<Fox::Scene::Scene>& GetScene(Fox::Graphics::Managers::Vulkan::SceneId id) {
						return scenes[id]; 
					}

					inline std::unique_ptr<Fox::Scene::Scene>& GetCurrentScene() {
						return scenes[currentSceneId];
					}

				private:
					SceneId currentSceneId = SceneId::MAIN_SCENE;
					std::unordered_map<Fox::Graphics::Managers::Vulkan::SceneId, std::unique_ptr<Fox::Scene::Scene>> scenes;
				};
			}
		}
	}
}