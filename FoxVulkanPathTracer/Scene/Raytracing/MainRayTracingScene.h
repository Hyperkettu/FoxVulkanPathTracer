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

                // --- Trees: generated, placed and changed while the scene renders (see Editor/TreeEditor) ---

                // Image files of one surface (paths from the working directory; empty = not used)
                struct SurfaceTextures {
                    std::string albedo, normal, roughness, metallic, occlusion, displacement;
                    bool normalGreenUp = true;       // Normal map convention: OpenGL (true) or DirectX
                    bool invertDisplacement = false; // Bright = low
                };

                // How a tree looks: its own textures and factors for bark and leaves
                struct TreeLook {
                    SurfaceTextures bark{ "Textures/bark/albedo.png", "Textures/bark/normal.png", "Textures/bark/roughness.png",
                        "Textures/bark/metallic.png", "Textures/bark/ao.png", "Textures/bark/displacement.png", true, false };
                    SurfaceTextures leaf{ "Textures/bark/leaf-albedo.png", "Textures/bark/leaf_normal.png", "Textures/bark/leaf-roughness.png",
                        "Textures/bark/leaf-metallic.png", "Textures/bark/leaf_ambient_occlusion.png", "Textures/bark/leaf_displacement.png", false, true };
                    glm::vec3 barkTint{ 1.0f };
                    glm::vec3 leafTint{ 1.0f };
                    float barkRoughness = 1.0f; // Multiplies the roughness texture
                    float leafRoughness = 1.0f;
                    float alphaCutoff = 0.5f;
                };

                struct Tree {
                    std::string name;
                    Fox::Graphics::Geometry::TreeSettings shape; // Its height maps come from the look
                    TreeLook look;
                    Fox::Animation::WindSkeleton::Settings wind;
                    ObjectTransform transform;

                    // Set by the scene
                    bool regenerate = true; // The shape (or a texture the geometry depends on) changed
                    std::shared_ptr<Fox::Graphics::Geometry::SkinnedMesh> mesh;
                    std::unique_ptr<Fox::Animation::WindSkeleton> skeleton;
                    DynamicMeshHandle handle;
                    bool placed = false;
                    float generationSeconds = 0.0f;
                };

                std::vector<Tree>& GetTrees() { return trees; }

                // A new tree standing on the ground at position (copying another's settings if given); returns its index
                size_t AddTree(const glm::vec3& position, const Tree* copyOf = nullptr);
                void RemoveTree(size_t tree);

                // Changes take effect with ApplyTreeChanges, which the renderer calls with the device idle
                void MarkTreesChanged() { treesChanged = true; }
                bool HaveTreesChanged() const { return treesChanged; }
                // Regenerates the trees marked for it and rebuilds the trees on the GPU. Returns false with error
                // set when they do not fit (the previous trees stay).
                bool ApplyTreeChanges(std::string& error);

                // The tree placed as a scene object, -1 if it is not a tree
                int GetTreeOfObject(int object) const;

                float GetGroundHeight() const;
                Fox::Scene::TextureLibrary& GetTextureLibrary() { return *textureLibrary; }

            private:
                std::vector<Tree> trees;
                bool treesChanged = false;
                std::unique_ptr<Fox::Scene::TextureLibrary> textureLibrary;
                float time = 0.0f;

                std::vector<Fox::Graphics::Vulkan::Material> MaterialsFor(const TreeLook& look);
                void GenerateTreeMesh(Tree& tree);

               
            };
        }
    }
}