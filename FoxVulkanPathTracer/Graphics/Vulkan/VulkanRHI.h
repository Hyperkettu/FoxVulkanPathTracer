#pragma once 

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			class VulkanRHI : public Fox::Graphics::RHI {
			public:
				VulkanRHI();
				virtual ~VulkanRHI();
				int32_t Initialize(const Fox::Graphics::RendererConfig& config) override;
				int32_t Destroy() override;
				virtual void Render() override;

				void RegisterInput(Fox::Input::InputManager& input) override;

			protected: 
				int32_t PickPhysicalDevice();
				int32_t FindGraphicsQueueFamily();
				int32_t CreateLogicalDevice();
				int32_t GetGraphicsQueue();
				int32_t GetPresentQueue();
				int32_t GetSurfaceFormat();
				int32_t CreateSwapchain();
				int32_t CreateCommandPoolsAndAssociatedBuffersForGraphicsQueue();
				int32_t CreateDefaultRenderPass();
				int32_t CreateInFlightSemaphoresAndFences();
				int32_t InitializePipelines();
				int32_t CreateUniformBuffers();

				void CreateDebugCallback();
				void DestroyDebugCallback();

				void LoadExtensionAPIFunctions();
				uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
				void UpdateUniformBuffer(uint32_t currentFrame);

				void Present(const std::unique_ptr<Fox::Graphics::Vulkan::Swapchain>& swapchain, uint32_t imageIndex, std::unique_ptr<Fox::Graphics::Vulkan::Semaphore>& semaphore);

			private:
				VkInstance instance;
				VkSurfaceKHR surface;
				VkPhysicalDevice physicalDevice;			
				VkDevice device;

				int32_t graphicsQueueFamily = -1;
				VkQueue graphicsQueue;

				VkQueue presentQueue;

				int32_t currentFrame = 0;

				VkSurfaceFormatKHR surfaceFormat;
				VkSurfaceCapabilitiesKHR capabilities;

				std::unique_ptr<Fox::Graphics::Vulkan::Swapchain> swapchain;

				std::vector<std::unique_ptr<Fox::Graphics::Vulkan::FrameResource>> frameResources;

				VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

				float rotationAngle = 0.0;

				std::unique_ptr<Fox::Graphics::Vulkan::Framebuffer> offscreenTarget;


				VkFormat depthFormat = VkFormat::VK_FORMAT_D24_UNORM_S8_UINT;

				Fox::Graphics::RendererConfig config;

				// input
				float cameraSpeed = 105.0f;
				float deltaTime = 1.0f / 60.0f; // TODO add real delta time
				Fox::Input::InputManager* inputManager;
				Fox::Core::Connection mouseInputBind;
				Fox::Core::Connection keyInputBind;

				std::unique_ptr<Fox::Scene::RayTracing::MainRayTracingScene> raytracingScene;

			};

		}
	}
}