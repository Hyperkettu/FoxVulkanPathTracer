#pragma once

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			// Vulkan backend. Each frame it path traces the scene into an HDR accumulation image
			// and then tone maps that image into the swapchain.
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
				void CreateAccumulationTexture();
				void CreateFrameDescriptors();
				void BindAccumulationTexture();

				// --- Per frame ---
				void HandleCameraInput(float deltaTime);
				void UpdateCameraUniforms(FrameResource& frameResource);
				void RecordFrame(FrameResource& frameResource, uint32_t imageIndex);
				VkResult Present(uint32_t imageIndex, VkSemaphore waitSemaphore);

				// Discards the accumulated samples; the next frame starts a fresh average.
				void ResetAccumulation() { accumulatedFrames = 0; }

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

				// Linear HDR running average written by the ray generation shader and read by the tone mapper.
				std::unique_ptr<Fox::Graphics::Vulkan::StorageTexture> accumulationTexture;
				std::unique_ptr<Fox::Graphics::Vulkan::PostProcessor> postProcessor;

				std::unique_ptr<Fox::Scene::RayTracing::MainRayTracingScene> raytracingScene;

				// --- Frame state ---
				uint32_t currentFrame = 0;      // Index into frameResources
				uint32_t frameCounter = 0;      // Monotonic, seeds the per-frame RNG
				uint32_t accumulatedFrames = 0; // Frames blended into accumulationTexture so far

				glm::vec3 lastCameraPosition{ 0.0f };
				glm::vec3 lastCameraForward{ 0.0f };

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
