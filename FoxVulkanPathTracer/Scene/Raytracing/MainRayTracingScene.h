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

                // Types of scene content (Scene/SceneContent.h)
                using SurfaceMaterial = Fox::Scene::SurfaceMaterial;
                using SurfaceTextures = Fox::Scene::SurfaceTextures;
                using TreeLook = Fox::Scene::TreeLook;
                using Tree = Fox::Scene::Tree;
                using Entity = Fox::ECS::Entity;

                struct Environment {
                    bool ground = true;
                    bool terrain = false; // A terrain (GetTerrain) instead of the flat ground plane
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

                Environment& GetEnvironment() { return environment; }
                // Camera paths (saved with the scene) and the camera's flight
                Fox::Scene::CameraManager& GetCameraManager() { return cameraManager; }

                // --- Terrain: in place of the ground plane while environment.terrain is on (see Editor/TerrainEditor) ---
                Fox::Scene::Terrain& GetTerrain() { return terrain; }
                bool IsTerrainPlaced() const { return terrainPlaced; }
                // The layers changed (textures, tiling, tint, ...) or the blend softness: their materials are replaced
                // in place, without a rebuild
                void ApplyTerrainLayers();
                // Sends what the brushes changed to the GPU; once per frame. strokeEnded: the terrain's acceleration
                // structure is built anew (during a stroke it is only refitted)
                void ApplyTerrainEdits(bool strokeEnded);
                // Height of the ground under a point: the terrain where there is terrain, otherwise the ground plane
                float GetGroundHeightAt(float x, float z) const;

                // --- Entities: the scene graph (Scene/SceneGraph.h). Entities with a MeshComponent are rendered. ---

                Fox::Scene::SceneGraph& GetSceneGraph() { return graph; }

                // New entities with a mesh (under parent, or a root), named after what they are
                Entity AddModel(const std::string& file, bool displacement, const glm::vec3& position, Entity parent = {});
                Entity AddSphereObject(const glm::vec3& position, float radius, const SurfaceMaterial& material, Entity parent = {});
                // A new tree standing on the ground at position (copying another's settings if given)
                Entity AddTree(const glm::vec3& position, const Tree* copyOf = nullptr, Entity parent = {});
                Entity CreateEmpty(const std::string& name, Entity parent = {});

                // Destroys the entity and everything under it; meshes among them leave the scene
                void DestroyEntity(Entity entity);
                // A copy of the entity and everything under it (trees are generated anew)
                Entity DuplicateEntity(Entity entity);

                // Gives the entity a mesh of this type (with defaults), or takes it away (hidden right away)
                Fox::Scene::MeshComponent& AddMeshComponent(Entity entity, Fox::Scene::MeshComponent::Type type);
                void RemoveMeshComponent(Entity entity);

                // A MeshComponent was added, removed or now shows something else (type, file, sphere): the scene
                // rebuilds what it needs to. previous: its type before, if it had one.
                void MeshChanged(const Fox::Scene::MeshComponent* now, const Fox::Scene::MeshComponent::Type* previous);

                // The entity rendered as a ray tracing scene object; null for the ground, terrain or nothing
                Entity GetEntityOfObject(int object) const;

                // Whether a placed mesh moved, appeared or disappeared since the last call (for the denoisers)
                bool TakeObjectsChanged() {
                    const bool changed = objectsChanged;
                    objectsChanged = false;
                    return changed;
                }

                // Entities with a tree mesh, in hierarchy order, and their trees
                std::vector<Entity> GetTreeEntities() const;
                Tree* GetTree(Entity entity);

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

                // Changes take effect with ApplyTreeChanges, which the renderer calls with the device idle
                void MarkTreesChanged() { treesChanged = true; }
                bool HaveTreesChanged() const { return treesChanged; }
                // Regenerates the trees marked for it and rebuilds the trees on the GPU. Returns false with error
                // set when they do not fit (the previous trees stay).
                bool ApplyTreeChanges(std::string& error);

                float GetGroundHeight() const;
                Fox::Scene::TextureLibrary& GetTextureLibrary() { return *textureLibrary; }

            private:
                Fox::Scene::SceneGraph graph;
                std::vector<Entity> objectEntities; // By ray tracing scene object

                // Moves and shows / hides the placed meshes as their entities are now
                void SyncMeshes();
                bool objectsChanged = false;
                // Meshes whose object is no longer theirs (the objects were rebuilt without them) are not placed
                void ForgetStalePlacements();
                Environment environment;
                Fox::Scene::CameraManager cameraManager;
                Fox::Scene::Terrain terrain;
                size_t terrainSurface = 0;
                bool terrainPlaced = false;
                void PlaceTerrain();
                void ResetTerrain();
                Fox::Graphics::Vulkan::Material TerrainSurfaceMaterial() const;
                std::vector<Fox::Graphics::Vulkan::Material> TerrainLayerMaterials();
                bool structureChanged = false;
                CameraPose pendingCamera;
                bool hasPendingCamera = false;

                // Loaded glTF files, by file and displacement: reused when added again or the scene rebuilds
                std::map<std::string, std::unique_ptr<Fox::Graphics::Geometry::Vulkan::Mesh>> loadedModels;
                Fox::Graphics::Geometry::Vulkan::Mesh& LoadModel(const std::string& file, bool displacement);

                bool treesChanged = false;
                std::unique_ptr<Fox::Scene::TextureLibrary> textureLibrary;
                float time = 0.0f;

                std::vector<Fox::Graphics::Vulkan::Material> MaterialsFor(const TreeLook& look);
                void GenerateTreeMesh(Tree& tree);

               
            };
        }
    }
}