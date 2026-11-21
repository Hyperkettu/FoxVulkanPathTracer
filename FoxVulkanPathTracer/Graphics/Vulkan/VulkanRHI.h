#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			// Vulkan backend. Each frame it path traces the scene into a G-buffer, runs the active
			// denoiser over it and tone maps the result into the swapchain.
			class VulkanRHI : public Fox::Graphics::RHI {
			public:
				VulkanRHI() = default;
				virtual ~VulkanRHI();

				bool Initialize(const Fox::Graphics::RendererConfig& config) override;
				void Destroy() override;
				void Render(float deltaTime) override;
				void Resize(uint32_t width, uint32_t height) override;

				void RegisterInput(Fox::Input::InputManager& input) override;
				bool HandleEvent(const SDL_Event& event) override;

			private:
				// --- Device setup ---
				bool CreateInstance();
				bool PickPhysicalDevice();
				bool FindGraphicsQueueFamily();
				bool CreateLogicalDevice();
				bool ChooseSurfaceFormat();
				bool ChooseDepthFormat();
				void LoadExtensionAPIFunctions();

				void CreateDebugCallback();
				void DestroyDebugCallback();

				// --- Resources ---
				void CreateSwapchain();
				void CreateGBuffer();
				void CreateFrameDescriptors();
				void BindGBuffer();
				void SelectDenoiser(size_t index);
				void SetUpscalingMode(RayTracing::UpscalingMode mode);
				void UpdateWindowTitle(float deltaTime);

				// --- Per frame ---
				void HandleCameraInput(float deltaTime);
				void UpdateCameraUniforms(FrameResource& frameResource, float deltaTime);
				glm::vec2 NextJitter() const;
				void RecordTrace(FrameResource& frameResource);                           // Path tracing + denoising
				void RecordToneMapping(FrameResource& frameResource, uint32_t imageIndex); // Into the swapchain image
				VkResult Present(uint32_t imageIndex, VkSemaphore waitSemaphore);

				// Tells the denoiser that the previous frame must not be reused (resize, lighting change...).
				void ResetHistory() { resetHistory = true; }

				// --- Editing (E key): select objects with the mouse, move / rotate / scale them with gizmos ---
				void SetEditMode(bool enabled);
				glm::vec2 MouseInDisplayPixels() const;
				Fox::Editor::TransformGizmo::View GizmoView() const;
				void ReadPickResult(uint32_t frameSlot);

				// --- User interface (Dear ImGui) ---
				void InitializeUserInterface();
				void BuildUserInterface();  // This frame's windows (the tree editor in edit mode)
				void ApplySceneChanges();   // Rebuilds what the editor changed (waits for the GPU)
				void RefreshTextureDescriptors();
				void WriteSceneDescriptors(FrameResource& frameResource); // Set 0: the scene's resources

			private:
				Fox::Graphics::RendererConfig config;

				VkInstance instance = VK_NULL_HANDLE;
				VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
				VkSurfaceKHR surface = VK_NULL_HANDLE;
				VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
				VkDevice device = VK_NULL_HANDLE;

				uint32_t graphicsQueueFamily = 0;
				VkQueue graphicsQueue = VK_NULL_HANDLE;
				VkQueue presentQueue = VK_NULL_HANDLE;

				VkSurfaceFormatKHR surfaceFormat{};
				VkSurfaceCapabilitiesKHR capabilities{};
				VkFormat depthFormat = VK_FORMAT_UNDEFINED;

				std::unique_ptr<Fox::Graphics::Vulkan::Swapchain> swapchain;

				// "Rendering finished" semaphores that gate present, one per swapchain image. They cannot live in
				// FrameResource: a semaphore handed to present is only free again once that image is re-acquired.
				std::vector<std::unique_ptr<Fox::Graphics::Vulkan::Semaphore>> renderFinishedSemaphores;

				std::vector<std::unique_ptr<Fox::Graphics::Vulkan::FrameResource>> frameResources;

				// Path tracer outputs of the current frame and the denoisers that can turn them into the final image.
				DeviceContext deviceContext;
				std::unique_ptr<RayTracing::GBuffer> gbuffer;
				std::vector<std::unique_ptr<RayTracing::Denoiser>> denoisers;
				size_t activeDenoiser = 0;

				// Render presets: how hard the path tracer works. Auto picks Balanced on laptop and mid-range GPUs,
				// Quality on the rest. Saved to Settings/Render.json.
				// Laptop30 and Laptop25: cheaper than Performance, capped lower, for a laptop GPU that should stay cool
				enum class RenderPreset { Auto, Quality, Balanced, Performance, Laptop30, Laptop25 };
				struct PresetSettings {
					RayTracing::UpscalingMode upscaling;
					uint32_t bounces;
					float frameCap; // Frames per second at most (0: as vsync allows)
					bool windLevelOfDetail; // Distant trees get their wind less often (see MainRayTracingScene)
				};
				void ApplyPresetSettings(const PresetSettings& settings);
				static const char* PresetName(RenderPreset preset);
				RenderPreset preset = RenderPreset::Auto;
				bool presetLoaded = false;
				std::string gpuName;
				uint64_t gpuMemory = 0;      // Bytes of its own memory
				bool modestGpu = false;      // A laptop or mid-range GPU (Auto: Balanced)
				RenderPreset EffectivePreset() const;
				PresetSettings SettingsOf(RenderPreset preset) const;
				void LoadRenderSettings();
				void SaveRenderSettings() const;
				// Picks a preset: its resolution (unless the user picked one since: U), bounces and frame cap
				void SetRenderPreset(RenderPreset newPreset);
				uint32_t maxBounces = 4;
				float frameCap = 0.0f;
				std::chrono::steady_clock::time_point lastFrameStart{};
				void WaitForFrameCap();

				// The path tracing pipeline (Settings/Render.json "pipeline"). Real-time path tracing traces every ray,
				// the camera's too. The hybrid path tracer rasterizes the first hits, Nanite style (cluster culling, a
				// visibility buffer: HybridRasterPass), and traces the rest: lights, shadows, bounces; its bounces look in
				// the previous frame's image first (screen traces, Lumen style) and trace only where that finds nothing.
				enum class RenderPipeline { RealTime, Hybrid };
				static const char* PipelineName(RenderPipeline pipeline);
				RenderPipeline renderPipeline = RenderPipeline::RealTime;
				bool screenTraces = true;
				bool occlusionCulling = true;
				bool rasterLod = true; // Trees: the raster's own (coarser) cut through their clusters, for its pixels
				bool softwareRaster = true; // Clusters small on screen: rasterized in a compute shader (Nanite style)
				void SetRenderPipeline(RenderPipeline pipeline);
				std::unique_ptr<RayTracing::HybridRasterPass> hybrid;
				uint32_t hybridVersion = 0; // The scene's raster version its clusters were made for
				void EnsureHybrid();
				void WriteHybridDescriptors(VkDescriptorSet set);
				bool denoisersSized = false; // The denoisers have their images (their output can be bound)

				// Render resolution relative to the display (the G-buffer has the render resolution)
				RayTracing::UpscalingMode upscalingMode = RayTracing::UpscalingMode::Native;
				bool upscalingModeChosenByUser = false; // Until then it follows the display size
				RayTracing::DenoiserFrameInfo denoiserFrame;

				// Extensions requested by the denoisers; kept alive because Vulkan is handed pointers into them
				std::vector<std::string> optionalInstanceExtensions;
				std::vector<std::string> optionalDeviceExtensions;
				bool shaderExecutionReordering = false; // VK_NV_ray_tracing_invocation_reorder enabled
				bool opacityMicromaps = false;          // VK_EXT_opacity_micromap enabled
				bool clusterLod = false;                // VK_NV_cluster_acceleration_structure enabled
				float shaderTime = 0.0f;                // Seconds, for animated surfaces (water)
				bool vsync = true; // V key
				std::unique_ptr<Fox::Graphics::Vulkan::PostProcessor> postProcessor;

				std::unique_ptr<Fox::Scene::RayTracing::MainRayTracingScene> raytracingScene;

				// --- Frame state ---
				uint32_t currentFrame = 0;      // Index into frameResources
				uint32_t frameCounter = 0;      // Monotonic, seeds the per-frame RNG
				bool resetHistory = true;       // The next frame must not reuse temporal history

				struct CameraState {
					glm::vec3 position{ 0.0f };
					glm::vec3 forward{ 0.0f };
					glm::vec3 right{ 0.0f };
					glm::vec3 up{ 0.0f };
				};
				CameraState previousCamera; // For motion vectors

				uint32_t samplesPerPixel = 1; // Paths traced per pixel and frame (DLSS Ray Reconstruction is designed for 1)

				// Frame rate shown in the window title
				float titleTimer = 0.0f;
				uint32_t titleFrames = 0;
				float consoleTimer = 0.0f;

				// GPU time per pass, shown in the window title
				std::unique_ptr<GpuProfiler> profiler;

				// --- Input ---
				float mouseSensitivity = 0.13f;
				Fox::Input::InputManager* inputManager = nullptr;
				Fox::Core::Connection mouseInputBind;
				Fox::Core::Connection keyInputBind;
				Fox::Core::Connection mouseButtonPressBind;
				Fox::Core::Connection mouseButtonReleaseBind;

				// --- Editing ---
				bool userInterface = false;      // ImGui is initialized
				// Bindless textures as ImGui draws them (the game's UI), registered once each
				std::map<int, VkDescriptorSet> imguiTextures;
				ImTextureID ImGuiTexture(int index);
				float uiScale = 0.0f;            // Sizes scale with the window height
				Fox::Editor::TreeEditor treeEditor;
				Fox::Editor::SceneEditor sceneEditor;
				Fox::Editor::SceneGraphEditor sceneGraphEditor;
				Fox::Editor::EditorShell shell;          // Menu bar, Find, window layout, status bar
				Fox::Editor::ContentBrowser contentBrowser;
				Fox::Editor::MeshPainter meshPainter;
				bool painterWasActive = false, terrainBrushWasActive = false; // Only one brush at a time
				Fox::ECS::Entity lastSelection;          // Selecting something new opens the Inspector
				bool showAgentPaths = true;               // Physics > Show Agent Paths: agents' ways in the view
				void DrawAgentPaths();
				void DrawVolumeBounds();
				std::vector<std::function<void()>> deferredCommands; // Menu commands run between frames

				// The editor (VulkanRHIEditor.cpp)
				void BuildEditor();
				void BuildMenus(const Fox::Scene::RayTracing::MainRayTracingScene::CameraPose& pose, const glm::vec3& spawn);
				void DrawControls();
				void Defer(std::function<void()> command);
				void RunDeferredCommands();
				// What a key does when released (the same as its menu command)
				void RunKeyCommand(SDL_Scancode key);

				// Play mode (F5): the editor's camera while the game plays, and whether it looks through the
				// game's camera (F8 lets go of it: a free camera)
				void TogglePlay();
				void FollowGameCamera();
				bool LooksThroughGameCamera() const;
				void DrawPlayToolbar();
				void DrawCameraFrustum();
				struct EditorView { glm::vec3 position{ 0.0f }, forward{ 0.0f, 0.0f, -1.0f }; float fov = 1.0f; };
				EditorView editorView;
				bool cameraEjected = false;
				glm::vec2 scriptMouseDelta{ 0.0f }; // Mouse movement since the last frame, for scripts
				void UpdateScriptInput();
				// The Input Actions window, and the binding it is listening for a key for
				void DrawInputActions();
				// The GPU Profiler window: frame times, passes, what the acceleration structures hold, memory
				void DrawProfiler();
				std::vector<float> cpuHistory = std::vector<float>(240, 0.0f);
				size_t cpuHistoryNext = 0;
				bool memoryBudget = false; // VK_EXT_memory_budget enabled: video memory use and budget
				std::vector<std::string> prefabFiles; // Create > Prefab, listed again every few seconds
				float prefabListAge = 1e9f;
				struct InputCapture { int action = -1; int axis = -1; bool negative = false; };
				InputCapture inputCapture;
				bool CapturingInput() const { return inputCapture.action >= 0 || inputCapture.axis >= 0; }
				Fox::Editor::CameraEditor cameraEditor;
				Fox::Editor::TerrainEditor terrainEditor;
				uint32_t textureVersion = 0;     // Of the scene's texture library, when the descriptors were written
				bool editMode = false;
				bool uiPointer = false; // Outside edit mode with UI buttons on the screen: a free pointer (right mouse looks)
				Fox::Editor::TransformGizmo gizmo;
				std::unique_ptr<OverlayRenderer> overlay;
				std::vector<OverlayRenderer::Vertex> overlayVertices;

				// Object picking: the path tracer reports the object seen at a pixel into pickBuffer (one entry per
				// frame in flight), read back once that frame has finished
				std::unique_ptr<Buffer> pickBuffer;
				bool pickRequested = false;
				bool clothCutting = false;               // Alt + Shift + drag: cutting cloth under the mouse
				glm::vec2 pickMouse{ 0.0f };            // Display pixels
				std::vector<bool> pickPending;          // Per frame in flight
				bool sceneChanged = false;              // An object moved this frame (restarts accumulation)
			};

		}
	}
}
