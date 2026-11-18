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

				// Render resolution relative to the display (the G-buffer has the render resolution)
				RayTracing::UpscalingMode upscalingMode = RayTracing::UpscalingMode::Native;
				bool upscalingModeChosenByUser = false; // Until then it follows the display size
				RayTracing::DenoiserFrameInfo denoiserFrame;

				// Extensions requested by the denoisers; kept alive because Vulkan is handed pointers into them
				std::vector<std::string> optionalInstanceExtensions;
				std::vector<std::string> optionalDeviceExtensions;
				bool shaderExecutionReordering = false; // VK_NV_ray_tracing_invocation_reorder enabled
				bool opacityMicromaps = false;          // VK_EXT_opacity_micromap enabled
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
				float uiScale = 0.0f;            // Sizes scale with the window height
				Fox::Editor::TreeEditor treeEditor;
				Fox::Editor::SceneEditor sceneEditor;
				Fox::Editor::SceneGraphEditor sceneGraphEditor;
				Fox::Editor::EditorShell shell;          // Menu bar, Find, window layout, status bar
				Fox::Editor::ContentBrowser contentBrowser;
				Fox::ECS::Entity lastSelection;          // Selecting something new opens the Inspector
				std::vector<std::function<void()>> deferredCommands; // Menu commands run between frames

				// The editor (VulkanRHIEditor.cpp)
				void BuildEditor();
				void BuildMenus(const Fox::Scene::RayTracing::MainRayTracingScene::CameraPose& pose, const glm::vec3& spawn);
				void DrawControls();
				void Defer(std::function<void()> command);
				void RunDeferredCommands();
				// What a key does when released (the same as its menu command)
				void RunKeyCommand(SDL_Scancode key);
				Fox::Editor::CameraEditor cameraEditor;
				Fox::Editor::TerrainEditor terrainEditor;
				uint32_t textureVersion = 0;     // Of the scene's texture library, when the descriptors were written
				bool editMode = false;
				Fox::Editor::TransformGizmo gizmo;
				std::unique_ptr<OverlayRenderer> overlay;
				std::vector<OverlayRenderer::Vertex> overlayVertices;

				// Object picking: the path tracer reports the object seen at a pixel into pickBuffer (one entry per
				// frame in flight), read back once that frame has finished
				std::unique_ptr<Buffer> pickBuffer;
				bool pickRequested = false;
				glm::vec2 pickMouse{ 0.0f };            // Display pixels
				std::vector<bool> pickPending;          // Per frame in flight
				bool sceneChanged = false;              // An object moved this frame (restarts accumulation)
			};

		}
	}
}
