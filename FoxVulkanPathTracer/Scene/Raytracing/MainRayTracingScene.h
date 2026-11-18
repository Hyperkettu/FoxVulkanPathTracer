#pragma once

#include <vector>
#include <memory>
#include <random>

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
                };

                // The sun a new scene starts with (an entity with a directional LightComponent)
                struct Sun {
                    glm::vec3 direction{ 0.876f, 0.22f, 0.479f }; // Towards the sun
                    glm::vec3 color{ 1.0f, 0.58f, 0.30f };
                    float intensity = 18.0f;                       // Irradiance = colour * intensity
                    float angularRadius = 1.2f;                    // Degrees
                };
                // A root entity "Sun" in the graph with a directional light shining from direction
                static Entity CreateSun(Fox::Scene::SceneGraph& graph, const Sun& sun = Sun{});
                // The rotation that turns an entity's light to shine from this direction (towards the light)
                static glm::quat RotationTowards(const glm::vec3& towardsLight);

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
                // instanceTrees: trees under it stay the same trees (instances: one geometry, edited together)
                Entity DuplicateEntity(Entity entity, bool instanceTrees = false);

                // A new entity with a light of this type at position (a directional light shines straight down)
                Entity AddLight(Fox::Scene::LightComponent::Type type, const glm::vec3& position, Entity parent = {});
                // Gives the entity a light of this type (with defaults), or takes it away (dark right away). The scene
                // rebuilds once to make room for it; changes to a light afterwards need no rebuild.
                Fox::Scene::LightComponent& AddLightComponent(Entity entity, Fox::Scene::LightComponent::Type type);
                void RemoveLightComponent(Entity entity);

                // Gives the entity a mesh of this type (with defaults), or takes it away (hidden right away)
                Fox::Scene::MeshComponent& AddMeshComponent(Entity entity, Fox::Scene::MeshComponent::Type type);
                void RemoveMeshComponent(Entity entity);

                // A MeshComponent was added, removed or now shows something else (type, file, sphere): the scene
                // rebuilds what it needs to. previous: its type before, if it had one.
                void MeshChanged(const Fox::Scene::MeshComponent* now, const Fox::Scene::MeshComponent::Type* previous);

                // The entity rendered as a ray tracing scene object; null for the ground, terrain or nothing
                Entity GetEntityOfObject(int object) const;

                // --- Water (WaterComponent, Scene/WaterSimulation.h) ---

                // A new entity with water, centred at position (its height is the water level)
                Entity AddWater(const glm::vec3& position, Entity parent = {});
                // The look of a water entity changed (colour, clarity, roughness): no rebuild
                void WaterLookChanged(Entity entity);
                // A click in edit mode: splashes the water it lands on (with splashing on); false if it hits none
                bool SplashWater(const glm::vec3& origin, const glm::vec3& direction);
                void RecordSimulations(VkCommandBuffer cmd, uint32_t frameSlot) override;

                // --- Physics (PhysicsComponent, Scene/Physics.h): Physics > Play (F5) ---
                // Entities with a PhysicsComponent become rigid bodies on the ground or terrain; dynamic ones fall,
                // collide and float in water, and their transforms follow. Stopping puts every entity back where it was.
                bool StartPhysics(std::string& error);
                void StopPhysics();
                bool IsPhysicsPlaying() const { return physicsPlaying; }
                size_t GetPhysicsBodyCount() const { return playingBodies.size(); }
                // New entities with a dynamic body: a ball (a sphere mesh) and a box (Models/box.glb, a metre across)
                Entity AddPhysicsBall(const glm::vec3& position);
                Entity AddPhysicsCrate(const glm::vec3& position);

                // Whether a placed mesh moved, appeared or disappeared since the last call (for the denoisers)
                bool TakeObjectsChanged() {
                    const bool changed = objectsChanged;
                    objectsChanged = false;
                    return changed;
                }

                // Entities with a tree mesh, in hierarchy order, and their trees
                std::vector<Entity> GetTreeEntities() const;
                Tree* GetTree(Entity entity);
                // How many entities show this tree (its instances)
                size_t CountUsers(const Tree* tree) const;
                // A new entity showing an existing tree (an instance of it), standing at position
                // rebuild false: the scene is not told yet (MarkStructureChanged / MarkTreesChanged later, e.g. once a
                // paint stroke ends, so the scene rebuilds once for many)
                Entity AddTreeInstance(const std::shared_ptr<Tree>& tree, const Fox::Scene::Transform& transform, const std::string& name,
                    Entity parent = {}, bool rebuild = true);
                Entity AddModelInstance(const std::string& file, bool displacement, const Fox::Scene::Transform& transform, const std::string& name,
                    Entity parent = {}, bool rebuild = true);
                // A model's lowest point in its own space (loads the model if it is not loaded yet; 0 if it cannot be)
                float GetModelBottom(const std::string& file, bool displacement);

                // Models, spheres and the environment changed: everything is rebuilt (RebuildScene)
                void MarkStructureChanged() { structureChanged = true; }
                bool HasStructureChanged() const { return structureChanged; }

                // Rebuilds the whole scene from its content: models (loaded once, then reused), spheres, ground,
                // sun and trees. Call with the device idle; the scene's GPU resources are all new afterwards.
                // Models that fail to load are left out and reported in error (returns false).
                bool RebuildScene(std::string& error);

                // An empty scene (default environment, the sun, no objects)
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
                // Water: drops (rain and wind gusts) and a rebuild once moved water has come to rest
                void UpdateWater(float dt);
                void PlaceWater(Entity entity, Fox::Scene::WaterComponent& water);
                struct PendingWater {
                    Entity entity;
                    std::vector<float> bed, depth;
                    Fox::Scene::WaterSimulation::Grid grid;
                    uint32_t droplets = 0;
                };
                std::vector<PendingWater> pendingWater; // Placed, simulations made after Build

                // Physics while it plays: the bodies of the entities, and where every entity was before it started
                std::unique_ptr<Fox::Scene::PhysicsWorld> physics;
                bool physicsPlaying = false;
                struct PlayingBody {
                    Entity entity;
                    uint32_t body = 0;
                    Fox::Scene::PhysicsComponent::Motion motion = Fox::Scene::PhysicsComponent::Motion::Dynamic;
                    glm::vec3 scale{ 1.0f };
                    bool floats = true;
                    float buoyancy = 1.0f;   // Water's density over the body's
                    float waterDrag = 0.5f;
                    float extent = 0.5f;     // Half its horizontal size, metres
                    bool inWater = false;
                    float wakeTimer = 0.0f;
                    glm::mat4 written{ 1.0f }; // The world matrix physics last gave it (anything else: it was moved by hand)
                };
                std::vector<PlayingBody> playingBodies;
                std::vector<std::pair<Entity, Fox::Scene::Transform>> playSnapshot;
                bool DescribeBody(Entity entity, const Fox::Scene::PhysicsComponent& component, Fox::Scene::PhysicsWorld::BodyDesc& desc);
                void UpdatePhysics(float dt);
                float frameTime = 0.0f;
                float waterMovedFor = -1.0f;            // Seconds since water was last moved (-1: not moving)
                std::mt19937 waterRandom{ 4242u };
                // The placed lights as their entities are now
                void SyncLights();
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
                std::map<std::string, float> modelBottoms; // GetModelBottom, by the same key
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