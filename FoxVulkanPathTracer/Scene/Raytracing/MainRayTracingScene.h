#pragma once

#include <vector>
#include <memory>

namespace Fox {

    namespace Scene {

        namespace RayTracing {

            class MainRayTracingScene : public Fox::Scene::RayTracing::RayTracingScene
            {
            public:
                MainRayTracingScene() = default;

                MainRayTracingScene(
                    VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    VkQueue queue,
                    uint32_t queueFamilyIndex) : Fox::Scene::RayTracing::RayTracingScene(device, physicalDevice, queue, queueFamilyIndex) {
                    Initialize();
                }

               

                virtual ~MainRayTracingScene() {}


                virtual void Initialize() override;
                virtual void Update(float deltaTime) override;

            private:
                // A bare tree swaying in the wind: a skinned mesh driven by angular springs
                std::unique_ptr<Fox::Animation::WindSkeleton> wind;
                size_t tree = 0; // Skinned mesh index
                float time = 0.0f;

               
            };
        }
    }
}