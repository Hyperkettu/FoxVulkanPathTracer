#pragma once

#include <vector>
#include <memory>
#include <random>
#include <unordered_set>
#include <unordered_map>
#include <functional>

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

                // Wind level of detail: trees small on screen get their wind every 2nd or 4th frame, the smallest stand
                // still. Off: every tree every frame.
                void SetWindLevelOfDetail(bool enabled) { windLevelOfDetail = enabled; }
                bool GetWindLevelOfDetail() const { return windLevelOfDetail; }
                // Trees (meshes; instances share one) last frame: blown every frame, every 2nd, every 4th, standing still
                struct WindStats { uint32_t every = 0, half = 0, quarter = 0, still = 0; };
                const WindStats& GetWindStats() const { return windStats; }

                // --- Scene content: what a scene file holds (Scenes/*.json, see SceneFile.cpp) ---

                // Types of scene content (Scene/SceneContent.h)
                using SurfaceMaterial = Fox::Scene::SurfaceMaterial;
                using SurfaceTextures = Fox::Scene::SurfaceTextures;
                using TreeLook = Fox::Scene::TreeLook;
                using Tree = Fox::Scene::Tree;
                using Entity = Fox::ECS::Entity;

                // Height fog: thickest at its base, thinning exponentially above it (Shaders/volumes.slang)
                struct Fog {
                    bool enabled = false;
                    float density = 0.02f;      // Extinction per metre at the base height
                    float falloff = 0.08f;      // Per metre: halves every ln(2) / falloff metres up
                    float baseHeight = -3.73f;  // Metres
                    glm::vec3 color{ 0.92f };   // Scattering albedo
                    float anisotropy = 0.6f;    // Forward scattering: glows around the sun
                };
                // A layer of clouds in the sky: noise drifting with the wind, lit (and shading) by the sun
                struct Clouds {
                    bool enabled = false;
                    float bottom = 250.0f;      // Metres
                    float top = 700.0f;
                    float coverage = 0.5f;      // 0..1
                    float density = 0.05f;      // Extinction per metre where densest
                    float size = 700.0f;        // Metres: the size of the noise's features
                    glm::vec3 wind{ 8.0f, 0.0f, 3.0f }; // m/s
                    float anisotropy = 0.7f;
                    float albedo = 0.98f;
                    bool shadows = true;        // Shade the ground
                };

                // Physics collision layers (Scene/Physics.h): their names and which collide with which
                struct PhysicsLayers {
                    std::array<std::string, Fox::Scene::PhysicsWorld::LAYER_COUNT> names{
                        "Default", "Ground", "Player", "Enemy", "Projectile", "Trigger", "Debris" };
                    // collides[i] bit j: layers i and j collide (kept symmetric)
                    std::array<uint32_t, Fox::Scene::PhysicsWorld::LAYER_COUNT> collides;
                    PhysicsLayers() { collides.fill(Fox::Scene::PhysicsWorld::ALL_LAYERS); }
                    std::string Name(int layer) const {
                        return layer >= 0 && layer < Fox::Scene::PhysicsWorld::LAYER_COUNT && !names[layer].empty()
                            ? names[layer] : "Layer " + std::to_string(layer);
                    }
                };

                // How the image is graded after tone mapping (Environment window > Image)
                using ImageGrade = Fox::Graphics::Vulkan::PostProcessor::Grade;

                // How the 2D UI scales with the window: its pixels are of this reference resolution, scaled by the window's
                // width (match 0), height (1) or between the two (logarithmically)
                struct UICanvas {
                    float referenceWidth = 1920.0f;
                    float referenceHeight = 1080.0f;
                    float match = 0.5f;
                    float ScaleFor(float width, float height) const {
                        const float w = std::log2(std::max(width, 1.0f) / std::max(referenceWidth, 1.0f));
                        const float h = std::log2(std::max(height, 1.0f) / std::max(referenceHeight, 1.0f));
                        return std::exp2(w + (h - w) * std::clamp(match, 0.0f, 1.0f));
                    }
                };

                struct Environment {
                    bool ground = true;
                    bool terrain = false; // A terrain (GetTerrain) instead of the flat ground plane
                    float groundHeight = -3.73f;
                    SurfaceMaterial groundMaterial{ glm::vec3(0.07f, 0.07f, 0.08f), 0.12f };
                    Fog fog;
                    Clouds clouds;
                    PhysicsLayers layers;
                    ImageGrade image;
                    UICanvas ui;
                    // One wind for every tree: degrees, where it blows to, from +x towards +z. The trees' own wind settings
                    // (Trees window) give its speed, gusts and how they respond.
                    float windDirection = 19.3f;
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

                // --- Play mode (F5): the game runs on a copy of the scene ---
                // Play starts the game systems (physics, agents, growing trees) on game time, which can be paused,
                // stepped a frame at a time and sped up or slowed down. Stopping puts the scene back as it was when
                // play started: entities created, destroyed, moved or changed while it played (by the game or by
                // hand) come back as they were. Saving while it plays saves the scene as it was.
                struct GameClock {
                    bool paused = false;
                    float timeScale = 1.0f;
                    int stepFrames = 0;    // Paused: frames still to step
                    double time = 0.0;     // Game seconds since play started
                    uint64_t frame = 0;    // Game frames since play started
                    float delta = 0.0f;    // This frame's step of game time
                };
                static constexpr float MAX_GAME_STEP = 0.1f; // Seconds: a long frame (a hitch, a breakpoint) counts as this
                static constexpr float STEP_FRAME = 1.0f / 60.0f;

                bool StartPlay(std::string& error);
                // restore: put the scene back as it was when play started (false: keep it as it is now)
                void StopPlay(bool restore = true);
                bool IsPlaying() const { return physicsPlaying; }
                const GameClock& GetClock() const { return clock; }
                void SetPaused(bool paused) { clock.paused = paused; clock.stepFrames = 0; }
                void SetTimeScale(float scale) { clock.timeScale = glm::clamp(scale, 0.0f, 16.0f); }
                // Paused: advance one frame of STEP_FRAME (pauses if it was not)
                void StepFrame() { clock.paused = true; clock.stepFrames++; }
                // Seconds of animation (game time while playing): drifting clouds, smoke, water ripples
                float GetAnimationTime() const { return time; }
                // The entity the game looks through: the enabled CameraComponent of highest priority (null: none)
                Entity FindGameCamera() const;
                // A new entity with a CameraComponent, looking from position along forward
                Entity AddGameCamera(const glm::vec3& position, const glm::vec3& forward, float fovDegrees);

                // --- Audio (SoundComponent, Audio/AudioEngine.h) ---
                Fox::Audio::AudioEngine* GetAudio() { return audio && audio->IsReady() ? audio.get() : nullptr; }
                // The entity's sound component plays (music: as the background music), or stops
                bool PlayEntitySound(Entity entity);
                void StopEntitySound(Entity entity, float fadeSeconds = 0.0f);
                bool IsEntitySoundPlaying(Entity entity) const;
                // Where sounds are heard from (the renderer: the camera, every frame)
                void SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);
                // The sound files in Sounds/
                std::vector<std::string> ListSounds() const;

                // --- Prefabs (PrefabComponent): entity trees saved in Prefabs/*.prefab.json, placed many times ---
                static constexpr const char* PREFAB_FOLDER = "Prefabs";
                // Saves the entity and everything under it as a new prefab (file receives its path) and makes the
                // entity an instance of it
                bool CreatePrefab(Entity root, const std::string& name, std::string& file, std::string& error);
                // A new instance of a prefab at position (under parent, or a root)
                Entity InstantiatePrefab(const std::string& file, const glm::vec3& position, Entity parent, std::string& error);
                // The instance the entity belongs to: itself or its nearest ancestor with a PrefabComponent (null: none)
                Entity PrefabRootOf(Entity entity) const;
                // What the instance changes from its prefab, by key (see PrefabComponent), as JSON text
                std::map<std::string, std::string> GetPrefabOverrides(Entity root);
                // The instance as it is becomes the prefab; every other instance updates, keeping its own overrides
                bool ApplyPrefab(Entity root, std::string& error);
                // Back to the prefab: one override (key), or all of them (empty key)
                bool RevertPrefab(Entity root, const std::string& key, std::string& error);
                // No longer an instance: what is under it stays, as ordinary entities
                void UnpackPrefab(Entity root);
                // The prefab files (Prefabs/*.prefab.json), and how many instances of one the scene has
                std::vector<std::string> ListPrefabs() const;
                size_t CountPrefabInstances(const std::string& file) const;

                // --- Scripts (ScriptComponent): C# in Scripts/*.cs, run while the game plays (Scripting/ScriptHost.h) ---
                // Off (null) when .NET or the scripting host is not there
                Fox::Scripting::ScriptHost* GetScripts() { return scripts && scripts->IsReady() ? scripts.get() : nullptr; }
                const std::string& GetScriptsError() const { return scriptsError; }
                // What the keyboard and mouse do this frame, for the scripts (the renderer fills it every frame)
                struct ScriptInput {
                    uint8_t keys[512] = {};
                    uint8_t previousKeys[512] = {};
                    uint8_t buttons[3] = {};
                    uint8_t previousButtons[3] = {};
                    glm::vec2 mouseDelta{ 0.0f };
                    glm::vec2 mousePosition{ 0.0f };
                };
                ScriptInput& GetScriptInput() { return scriptInput; }
                // Named actions and axes for the scripts (Settings/Input.json; the Input Actions window edits them)
                Fox::Input::InputActions& GetInputActions() { return inputActions; }
                bool SaveInputActions(std::string& error) const { return inputActions.Save(Fox::Input::InputActions::FILE, error); }

                // --- Physics (PhysicsComponent, Scene/Physics.h): Play (F5) ---
                // Entities with a PhysicsComponent become rigid bodies on the ground or terrain; dynamic ones fall,
                // collide and float in water, and their transforms follow. Stopping puts every entity back where it was.
                bool StartPhysics(std::string& error);
                void StopPhysics();
                bool IsPhysicsPlaying() const { return physicsPlaying; }
                size_t GetPhysicsBodyCount() const { return playingBodies.size(); }
                // New entities with a dynamic body: a ball (a sphere mesh) and a box (Models/box.glb, a metre across)
                Entity AddPhysicsBall(const glm::vec3& position);
                Entity AddPhysicsCrate(const glm::vec3& position);

                // --- Navigation (NavAgentComponent, Scene/Navigation.h) ---
                // Agents walk to their targets while the scene plays (F5, with physics), on paths found with A* over
                // the ground: around meshes in the way, slopes too steep and water too deep. In edit mode their
                // paths are planned (and shown) as they are moved.
                // The way for the agent from one point to another (status says how it went)
                // grid: receives the grid it was planned on (where obstacles, slopes and water block it)
                Fox::Scene::NavigationGrid::Result FindPath(Entity agent, const glm::vec3& from, const glm::vec3& to, std::string& status,
                    std::shared_ptr<const Fox::Scene::NavigationGrid>* grid = nullptr);
                // Sends the agent to a point on the ground (it plans its way at once)
                void SetNavTarget(Entity agent, const glm::vec3& target);
                // Every agent plans again (obstacles or the ground changed)
                void InvalidatePaths();
                // A new agent: a ball with a nose, facing +z
                Entity AddNavAgent(const glm::vec3& position);
                // Where a ray from the camera meets the ground (terrain or ground plane)
                bool RaycastGround(const glm::vec3& origin, const glm::vec3& direction, glm::vec3& hit) const;

                // --- Volumetrics: fog and clouds (Environment), smoke (VolumeComponent) ---
                // Fills the path tracer's media settings; returns how many smoke volumes there were (only the
                // first MAX_VOLUMES are used)
                size_t FillVolumeConstants(Fox::Graphics::Vulkan::CameraUBO& constants) const;
                // A new entity with a smoke volume at position
                Entity AddSmoke(const glm::vec3& position, bool glowing = false);
                // An entity scattering grass around position (ClutterComponent)
                Entity AddClutter(const glm::vec3& position);
                // An entity emitting an effect (ParticleComponent)
                Entity AddParticles(const glm::vec3& position, Fox::Scene::ParticleComponent::Effect effect);
                // Text (TextComponent) and the UI's sprites (SpriteComponent)
                Entity AddText(const glm::vec3& position, Fox::Scene::TextComponent::Space space, Fox::Scene::TextComponent::Style style,
                    bool billboard = false, const std::string& font = "", Entity parent = {});
                Entity AddSprite(Entity parent = {}, bool panel = false);
                // A UI in the world (UICanvasComponent) with a panel and a text in it
                Entity AddWorldCanvas(const glm::vec3& position, bool billboard);

                // The camera this frame (set before the frame's update): billboards turn to it, the screen's 3D text is
                // held in front of it (UpdateViewObjects)
                void SetViewCamera(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up, float fovY, float aspect);
                void UpdateViewObjects();

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
                // Clutter (ClutterComponent): scattered with the rebuild; turned, swayed and followed every frame
                void PlaceClutter(const std::vector<Entity>& entities, std::string& problems);
                void UpdateClutter(float dt);
                // Particles (ParticleComponent): their surfaces and lights made with the rebuild; simulated every frame
                void PlaceParticles(const std::vector<Entity>& entities, std::string& problems);
                void UpdateParticles(float dt);
                void SimulateParticles(Entity entity, Fox::Scene::ParticleComponent& emitter, float dt);
                // Orbs: the slots kept for orbs (the rest are motes)
                static size_t OrbSlots(const Fox::Scene::ParticleComponent& emitter) {
                    return std::max<size_t>(1, emitter.particles.size() / 4);
                }
                // Water surfaces' rectangles and levels (leaves float on them, clutter keeps out of them)
                struct Pond { glm::vec3 centre; glm::vec2 half; };
                std::vector<Pond> ponds;
                float particleTime = 0.0f;
                // Text: its geometry made with the rebuild; its object following its entity (or the camera)
                void PlaceTexts(const std::vector<Entity>& entities, std::string& problems);
                void PlaceCanvases(const std::vector<Entity>& entities);
                static std::string CanvasKey(const Fox::Scene::UICanvasComponent& canvas);
                void UpdateTexts();
                static std::string TextKey(const Fox::Scene::TextComponent& text);
                struct ViewCamera {
                    glm::vec3 position{ 0.0f }, forward{ 0.0f, 0.0f, -1.0f }, up{ 0.0f, 1.0f, 0.0f };
                    float fovY = 1.0f, aspect = 1.5f;
                } viewCamera;
                static constexpr float SCREEN_TEXT_DISTANCE = 0.25f; // Metres in front of the camera the screen's 3D text is held
                static void MakeGrassTuft(const Fox::Scene::ClutterComponent& clutter, std::vector<Fox::Graphics::Vulkan::Vertex>& vertices,
                    std::vector<uint32_t>& indices);
                bool windLevelOfDetail = true;
                WindStats windStats;
                uint32_t windFrame = 0;
                void PlaceWater(Entity entity, Fox::Scene::WaterComponent& water);
                struct PendingWater {
                    Entity entity;
                    std::vector<float> bed, depth;
                    Fox::Scene::WaterSimulation::Grid grid;
                    uint32_t droplets = 0;
                };
                std::vector<PendingWater> pendingWater; // Placed, simulations made after Build

                std::unique_ptr<Fox::Audio::AudioEngine> audio;
                void UpdateAudio();

                // Prefabs: loaded files (kept until they change on disk; their trees shared by all instances)
                struct PrefabAsset {
                    std::string entities;    // JSON list, as in a scene file
                    std::string treeAssets;  // JSON list
                    std::vector<std::shared_ptr<Tree>> trees;
                    std::filesystem::file_time_type written{};
                };
                std::map<std::string, std::shared_ptr<PrefabAsset>> prefabs;
                std::shared_ptr<PrefabAsset> LoadPrefab(const std::string& file, std::string& error);
                bool SavePrefabFile(Entity root, const std::string& file, std::string& error);
                // Fills an instance (in graph, which may not be the scene's yet): its components and what is under it
                bool InstantiateInto(Fox::Scene::SceneGraph& target, Entity root, int depth, std::string& error);
                // Every instance in the graph whose ancestors are not instances (they fill their own)
                void InstantiateAll(Fox::Scene::SceneGraph& target);
                // Empties an instance and fills it again from its prefab with these overrides
                void RebuildPrefabInstance(Entity root, const std::map<std::string, std::string>& overrides);
                // Every instance's overrides from how it is now (before the scene is saved)
                void RefreshPrefabOverrides();

                // Scripts: the host, and which running script belongs to which entity
                friend struct ScriptBindings;
                std::unique_ptr<Fox::Scripting::ScriptHost> scripts;
                std::string scriptsError;
                ScriptInput scriptInput;
                Fox::Input::InputActions inputActions;
                std::unordered_map<int64_t, Entity> runningScripts;
                void InitializeScripts();
                void UpdateScripts(float step); // Starts scripts added since, stops removed ones, runs them
                void StopScripts();

                // Play mode: the clock, and the scene as it was when play started
                GameClock clock;
                float AdvanceClock(float realDelta);
                struct PlaySnapshot {
                    std::string entities, treeAssets;           // The entities as saved (JSON)
                    std::vector<std::shared_ptr<Tree>> trees;   // The trees they refer to, by asset id
                    Environment environment;
                    std::vector<Entity> order;                  // Every entity, depth first
                    size_t rebuilds = 0;
                    // Put component values back in place (apply), or check that they still fit (the same entities
                    // with the same components): the quick way back, without a rebuild
                    std::vector<std::function<bool(bool apply)>> restores;
                };
                std::unique_ptr<PlaySnapshot> playScene;
                size_t rebuildCount = 0;
                void TakePlaySnapshot();
                void RestorePlaySnapshot();

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
                // A body for the entity's component, playing from now; false if its shape could not be made
                bool AddPlayingBody(Entity entity, Fox::Scene::PhysicsComponent& component);
                std::unordered_set<Entity> failedBodies; // Not tried again while it plays
                void UpdatePhysics(float dt);
                // Contacts and triggers that began or ended this frame, to the entities' scripts
                void DispatchContacts(const std::vector<Fox::Scene::PhysicsWorld::ContactEvent>& events);
                // Agents: plans their paths, and walks them while playing
                void UpdateNavigation(float dt);
                void StartNavigation();
                // A model's triangles (without displacement) with its glTF nodes' transforms applied, as the renderer
                // places them: obstacles for navigation, shapes for physics. Kept by file; null when it cannot be loaded.
                struct ModelShape {
                    std::vector<glm::vec3> positions;
                    std::vector<uint32_t> indices;
                    glm::vec3 low{ 0.0f }, high{ 0.0f };
                };
                std::map<std::string, std::shared_ptr<ModelShape>> modelShapes;
                const ModelShape* ModelShapeOf(const std::string& file);
                // An enabled agent or part of one (they move: not obstacles)
                bool IsNavAgentPart(Entity entity) const;
                uint64_t obstacleGeneration = 0; // Counts the frames obstacles moved in
                bool meshesMoved = false;        // Set by SyncMeshes: obstacles may be elsewhere
                bool pathsStale = false;         // Edit mode: agents plan again once things stop moving
                float meshesQuietFor = 0.0f;
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