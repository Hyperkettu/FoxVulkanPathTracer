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
				void UpdateWindowTitle(float deltaTime);

				// --- Per frame ---
				void HandleCameraInput(float deltaTime);
				void UpdateCameraUniforms(FrameResource& frameResource, float deltaTime);
				glm::vec2 NextJitter() const;
				void RecordFrame(FrameResource& frameResource, uint32_t imageIndex);
				VkResult Present(uint32_t imageIndex, VkSemaphore waitSemaphore);

				// Tells the denoiser that the previous frame must not be reused (resize, lighting change...).
				void ResetHistory() { resetHistory = true; }

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
				RayTracing::DenoiserFrameInfo denoiserFrame;

				// Extensions requested by the denoisers; kept alive because Vulkan is handed pointers into them
				std::vector<std::string> optionalInstanceExtensions;
				std::vector<std::string> optionalDeviceExtensions;
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

				uint32_t samplesPerPixel = 2; // Paths traced per pixel and frame

				// Frame rate shown in the window title
				float titleTimer = 0.0f;
				uint32_t titleFrames = 0;

				// --- Input ---
				float cameraSpeed = 10.0f;
				float mouseSensitivity = 0.13f;
				Fox::Input::InputManager* inputManager = nullptr;
				Fox::Core::Connection mouseInputBind;
				Fox::Core::Connection keyInputBind;
			};

		}
	}
}
