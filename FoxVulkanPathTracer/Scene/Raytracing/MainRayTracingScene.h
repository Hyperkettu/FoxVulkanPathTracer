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

                // --- Scene content: what a scene file holds (Scenes/*.json, see SceneFile.cpp) ---

                // A simple material for generated surfaces (ground, spheres)
                struct SurfaceMaterial {
                    glm::vec3 color{ 0.8f };
                    float roughness = 0.5f;
                    float metallic = 0.0f;
                    float transmission = 0.0f;           // 1: glass
                    float ior = 1.5f;                    // Glass: solid (refracting) when > 0
                    glm::vec3 attenuationColor{ 1.0f };  // Glass: colour left after attenuationDistance metres
                    float attenuationDistance = 0.0f;    // 0: clear

                    Fox::Graphics::Vulkan::Material ToMaterial() const;
                };

                // A glTF model placed in the scene
                struct Model {
                    std::string name;
                    std::string file;          // .glb / .gltf, from the working directory
                    bool displacement = false; // Relief reconstructed from its normal maps (slower to load)
                    ObjectTransform transform;
                    bool placed = false;       // Set by the scene: object is its index
                    size_t object = 0;
                };

                struct Sphere {
                    std::string name;
                    float radius = 1.0f;
                    SurfaceMaterial material;
                    ObjectTransform transform;
                    bool placed = false;
                    size_t object = 0;
                };

                struct Environment {
                    bool ground = true;
                    float groundHeight = -3.73f;
                    SurfaceMaterial groundMaterial{ glm::vec3(0.07f, 0.07f, 0.08f), 0.12f };
                    bool sun = true;
                    glm::vec3 sunDirection{ 0.876f, 0.22f, 0.479f }; // Towards the sun
                    glm::vec3 sunColor{ 1.0f, 0.58f, 0.30f };
                    float sunIntensity = 18.0f;                       // Irradiance = colour * intensity
                    float sunAngularRadius = 1.2f;                    // Degrees
                };

                struct CameraPose {
                    glm::vec3 position{ 29.0f, -2.6f, 17.0f };
                    glm::vec3 target{ 4.0f, -0.6f, -3.0f };
                };

                static constexpr const char* DEFAULT_SCENE_FILE = "Scenes/default.json";

                std::vector<Model>& GetModels() { return models; }
                std::vector<Sphere>& GetSpheres() { return spheres; }
                Environment& GetEnvironment() { return environment; }

                size_t AddModel(const std::string& file, bool displacement, const glm::vec3& position);
                size_t AddSphereObject(const glm::vec3& position, float radius, const SurfaceMaterial& material);

                // Removes the model, sphere or tree placed as this scene object; false if it is none of them
                bool RemoveObject(int object);

                // Models, spheres and the environment changed: everything is rebuilt (RebuildScene)
                void MarkStructureChanged() { structureChanged = true; }
                bool HasStructureChanged() const { return structureChanged; }

                // Rebuilds the whole scene from its content: models (loaded once, then reused), spheres, ground,
                // sun and trees. Call with the device idle; the scene's GPU resources are all new afterwards.
                // Models that fail to load are left out and reported in error (returns false).
                bool RebuildScene(std::string& error);

                // An empty scene (default environment, no objects)
                void ClearContent();

                // Saves the content, where objects are now (gizmos included) and the camera
                bool SaveToFile(const std::string& path, const CameraPose& camera, std::string& error);
                // Replaces the content with a file's (RebuildScene shows it); camera receives the saved view
                bool LoadFromFile(const std::string& path, CameraPose& camera, std::string& error);

                // A camera pose to apply, after the scene file it came from was loaded
                bool TakePendingCamera(CameraPose& camera) {
                    if (!hasPendingCamera) return false;
                    camera = pendingCamera;
                    hasPendingCamera = false;
                    return true;
                }

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
                std::vector<Model> models;
                std::vector<Sphere> spheres;
                Environment environment;
                bool structureChanged = false;
                CameraPose pendingCamera;
                bool hasPendingCamera = false;

                // Loaded glTF files, by file and displacement: reused when added again or the scene rebuilds
                std::map<std::string, std::unique_ptr<Fox::Graphics::Geometry::Vulkan::Mesh>> loadedModels;
                Fox::Graphics::Geometry::Vulkan::Mesh& LoadModel(const std::string& file, bool displacement);

                // Where gizmos moved the objects, back into the content
                void SyncTransforms();

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