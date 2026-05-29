#pragma once

#include <vector>
#include <memory>

namespace Fox {

    namespace Scene {

        class MainScene: public Fox::Scene::Scene
        {
        public:
            MainScene() = default;  
            virtual ~MainScene() {}


            virtual void Initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkSurfaceCapabilitiesKHR capabilities) override;
            virtual void Update(float deltaTime) override;
            virtual void SetUpCamera(VkSurfaceCapabilitiesKHR capabilities) override;

          //  void AddEntity(const std::shared_ptr<Fox::Scene::Entity>& entity);
          //  void Update(float deltaTime);

            void AddMeshToScene(Fox::Graphics::Managers::Vulkan::MeshResource id);

        protected:
        };
    }
}