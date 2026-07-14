#pragma once

#include <vector>
#include <memory>
#include <cstring>

namespace Fox {

    namespace Scene {

        struct Batch {
            glm::mat4 modelTransform;
            Fox::Graphics::Managers::Vulkan::MeshResource meshId;
        };

        class Scene
        {
        public:
            Scene() = default;
            virtual ~Scene() {
                vertexBuffer = nullptr;
                indexBuffer = nullptr;
            }

            virtual void Initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkSurfaceCapabilitiesKHR capabilities) = 0;

          //  void AddEntity(const std::shared_ptr<Fox::Scene::Entity>& entity);
            virtual void Update(float deltaTime) = 0;

            const std::shared_ptr<Fox::Graphics::Vulkan::Camera>& GetMainCamera() const {
                return mainCamera;
            }

            inline std::unique_ptr<Fox::Graphics::Vulkan::VertexBuffer>& GetVertexBuffer() {
                return vertexBuffer;
            }

            inline std::unique_ptr<Fox::Graphics::Vulkan::IndexBuffer>& GetIndexBuffer() {
                return indexBuffer;
            }

            inline const std::vector<Fox::Graphics::Vulkan::MeshInfo>& GetSceneMeshInfos() const {
                return sceneMeshInfos;
            }

            virtual void SetUpCamera(VkSurfaceCapabilitiesKHR capabilities) = 0;

        protected:


            void BuildBuffers(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue) {
                vertexBuffer = std::make_unique<Fox::Graphics::Vulkan::VertexBuffer>();
                vertexBuffer->Create(device, physicalDevice, commandPool, queue, sceneVertices, "Scene Vertex Buffer");

                indexBuffer = std::make_unique<Fox::Graphics::Vulkan::IndexBuffer>();
                indexBuffer->Create(device, physicalDevice, commandPool, queue, sceneIndices, "Scene Index Buffer");
            }

         //   std::vector<std::shared_ptr<Fox::Scene::Entity>> entities;
            std::shared_ptr<Fox::Graphics::Vulkan::Camera> mainCamera;

            // rendering, for now use only one vertex batch buffer
            std::vector<Fox::Graphics::Vulkan::MeshInfo> sceneMeshInfos;
            std::unique_ptr<Fox::Graphics::Vulkan::VertexBuffer> vertexBuffer;
            std::unique_ptr<Fox::Graphics::Vulkan::IndexBuffer> indexBuffer;

            std::vector<Fox::Graphics::Vulkan::Vertex> sceneVertices;
            std::vector<uint32_t> sceneIndices;


            std::vector<Fox::Scene::Batch> batches;

        };
    }
}