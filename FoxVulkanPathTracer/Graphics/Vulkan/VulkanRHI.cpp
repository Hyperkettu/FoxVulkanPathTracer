#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			VulkanRHI::VulkanRHI() {
			}

			VulkanRHI::~VulkanRHI() {
				vkDeviceWaitIdle(device);
				Destroy();
			}

			int32_t VulkanRHI::Initialize(const Fox::Graphics::RendererConfig& config) {
				this->config = config;

				// Create Vulkan instance
				VkApplicationInfo appInfo{};
				appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
				appInfo.pApplicationName = "Fox Vulkan Path Tracing Renderer";
				appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
				appInfo.pEngineName = "No Engine";
				appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
				appInfo.apiVersion = VK_API_VERSION_1_4;

				VkInstanceCreateInfo instanceCreateInfo{};
				instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
				instanceCreateInfo.pApplicationInfo = &appInfo;
				instanceCreateInfo.enabledExtensionCount = config.extensionCount;
				instanceCreateInfo.ppEnabledExtensionNames = config.instanceExtensions.data();
				instanceCreateInfo.enabledLayerCount = config.layerCount;
				instanceCreateInfo.ppEnabledLayerNames = config.layers.data();

				VkResult result = vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
				if (result != VK_SUCCESS) {
					std::cerr << "vkCreateInstance failed: " << result << "\n";
					return EXIT_FAILURE;
				}

#ifdef _DEBUG
				CreateDebugCallback();
#endif

				if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(config.windowHandle), instance, &surface)) {
					std::cerr << "SDL_Vulkan_CreateSurface failed\n";
					return 0;
				}

				frameResources.resize(config.MAX_FRAMES_IN_FLIGHT);
				for(auto i = 0; i < config.MAX_FRAMES_IN_FLIGHT; i++)
				{
					frameResources[i] = std::make_unique<FrameResource>();
					frameResources[i]->frameIndex = i;
				}

				// currently just choose the first one
				if (!PickPhysicalDevice()) {
					std::cerr << "Picking physical device failed\n";
					return 0;
				}

				if (!FindGraphicsQueueFamily()) {
					std::cerr << "Finding graphics queue family failed\n";
					return 0;
				}

				if (!CreateLogicalDevice()) {
					std::cerr << "Creating logical device failed\n";
					return 0;
				}

				LoadExtensionAPIFunctions();

				if (!GetGraphicsQueue()) {
					std::cerr << "Getting graphics queue failed\n";
					return 0;
				}
				if (!GetPresentQueue()) {
					std::cerr << "Getting present queue failed\n";
					return 0;
				}


				if (!GetSurfaceFormat()) {
					std::cerr << "Getting surface format failed\n";
					return 0;
				}

				if (!CreateDefaultRenderPass()) {
					std::cerr << "Creating default render pass failed\n";
					return 0;
				}

				vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

				if (!CreateInFlightSemaphoresAndFences()) {
					std::cerr << "Creating sync objects failed\n";
					return 0;
				}

				if (!CreateCommandPoolsAndAssociatedBuffersForGraphicsQueue()) {
					std::cerr << "Creating graphics queue command resources failed\n";
					return 0;
				}

				Fox::Graphics::Managers::Vulkan::TextureManager::Get().Initialize(device, physicalDevice, capabilities, surfaceFormat, depthFormat, graphicsQueue, frameResources[0]->commandPool);

				if (!CreateSwapchain()) {
					std::cerr << "Creating swapchain failed\n";
					return 0;
				}

				Fox::Graphics::Managers::Vulkan::MeshManager::Get().Initialize(device);
				Fox::Graphics::Managers::Vulkan::SceneManager::Get().Initialize(device, physicalDevice, frameResources[0]->commandPool->Get(), graphicsQueue, capabilities);

				Fox::Graphics::Vulkan::RayTracing::RayTracingPipeline::RegisterExtensionFunctions(device);
				Fox::Scene::RayTracing::RayTracingScene::RegisterExtensionFunctions(device);

				raytracingScene = std::make_unique <Fox::Scene::RayTracing::MainRayTracingScene>(device, physicalDevice, graphicsQueue, graphicsQueueFamily);

				for (auto i = 0; i < config.MAX_FRAMES_IN_FLIGHT; i++) {

					auto frameResource = frameResources[i].get();

					frameResource->storageTexture = std::make_unique<Fox::Graphics::Vulkan::StorageTexture>(device, physicalDevice, VkExtent3D{ .width = capabilities.currentExtent.width, .height = capabilities.currentExtent.height, .depth = 1 },
						VK_FORMAT_R8G8B8A8_UNORM,
						VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

					VkImageMemoryBarrier barrier{
						.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
						.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
						.newLayout = VK_IMAGE_LAYOUT_GENERAL,
						.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
						.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
						.image = frameResource->storageTexture->GetImage(),
						.subresourceRange = {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.baseMipLevel = 0,
							.levelCount = 1,
							.baseArrayLayer = 0,
							.layerCount = 1
						}
					};

					VkImageSubresourceRange subresourceRange = {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.baseMipLevel = 0,
							.levelCount = 1,
							.baseArrayLayer = 0,
							.layerCount = 1
					};

					barrier.srcAccessMask = 0;
					barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

					std::unique_ptr<Fox::Graphics::Vulkan::CommandList> cmdList = std::make_unique<Fox::Graphics::Vulkan::CommandList>(device, frameResources[0]->commandPool->Get());

					cmdList->Begin()
						.TransitionImageLayout(frameResource->storageTexture->GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
							subresourceRange, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VkPipelineStageFlagBits::VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR)
						.End().SubmitAndWait(graphicsQueue);

				}

				if (!CreateUniformBuffers()) {
					std::cerr << "Creating uniform buffer failed\n";
					return 0;
				}
				if (!InitializePipelines()) {
					std::cerr << "Creating pipelines failed\n";
					return 0;
				}

				std::vector<VkImageView> attachments = {
					Fox::Graphics::Managers::Vulkan::TextureManager::Get().GetRenderTargetTexture(Fox::Graphics::Managers::Vulkan::RenderTargetTexture::DEFAULT_RENDER_TARGET)->GetView(),
					Fox::Graphics::Managers::Vulkan::TextureManager::Get().GetDepthTexture(Fox::Graphics::Managers::Vulkan::DepthTexture::DEFAULT_DEPTH_STENCIL)->GetView()
				};

				Fox::Graphics::Managers::Vulkan::RenderPassManager::Get().Initialize(
					device,
					capabilities,
					surfaceFormat,
					depthFormat
				);


				offscreenTarget = std::make_unique<Fox::Graphics::Vulkan::Framebuffer>(
					device,
					Fox::Graphics::Managers::Vulkan::RenderPassManager::Get().GetPass(Fox::Graphics::Managers::Vulkan::RenderPass::OFFSCREEN)->Get(),
					attachments,
					capabilities.currentExtent.width,
					capabilities.currentExtent.height
				);

				

				std::cout << "Vulkan instance created successfully.\n";
				return 1;
			}

			int32_t VulkanRHI::Destroy() {
				std::cout << "Destroy Vulkan RHI" << std::endl;

				for (uint32_t i = 0u; i < frameResources.size(); i++)
				{
					frameResources[i]->Destroy(device);
				}

				offscreenTarget = nullptr;

				swapchain = nullptr;
				raytracingScene = nullptr;	
				

				Fox::Graphics::Managers::Vulkan::RenderPassManager::Get().Destroy(); 
				Fox::Graphics::Managers::Vulkan::PipelineManager::Get().Destroy();
				Fox::Graphics::Managers::Vulkan::DescriptorManager::Get().Destroy();
				Fox::Graphics::Managers::Vulkan::TextureManager::Get().Destroy();
				Fox::Graphics::Managers::Vulkan::MeshManager::Get().Destroy();
				Fox::Graphics::Managers::Vulkan::SceneManager::Get().Destroy();
				

				vkDestroyDevice(device, nullptr);
				vkDestroySurfaceKHR(instance, surface, nullptr);

#ifdef _DEBUG
				DestroyDebugCallback();
#endif

				vkDestroyInstance(instance, nullptr);
				return 1;
			}

			int32_t VulkanRHI::PickPhysicalDevice() {
				// Enumerate physical devices
				uint32_t gpuCount = 0;
				vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
				if (gpuCount == 0) {
					std::cerr << "No Vulkan-compatible GPU found.\n";
					return 0;
				}
				std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
				vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data());
				physicalDevice = physicalDevices[0];

				return 1;
			}

			int32_t VulkanRHI::FindGraphicsQueueFamily() {
				// Find graphics queue family
				uint32_t queueFamilyCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
				std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
				vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

				graphicsQueueFamily = -1;
				for (int i = 0; i < queueFamilies.size(); ++i) {
					VkBool32 presentSupport = false;
					vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
					if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport) {
						graphicsQueueFamily = i;
						break;
					}
				}
				if (graphicsQueueFamily == -1) {
					std::cerr << "Failed to find suitable queue family.\n";
					return 0;
				}

				return 1;
			}

			int32_t VulkanRHI::CreateLogicalDevice() {
				float queuePriority = 1.0f;
				VkDeviceQueueCreateInfo queueCreateInfo{};
				queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueCreateInfo.queueFamilyIndex = graphicsQueueFamily;
				queueCreateInfo.queueCount = 1;
				queueCreateInfo.pQueuePriorities = &queuePriority;

				const char* deviceExtensions[] = {
					VK_KHR_SWAPCHAIN_EXTENSION_NAME,
					VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
					VK_EXT_MESH_SHADER_EXTENSION_NAME,
					VK_KHR_SPIRV_1_4_EXTENSION_NAME,
					VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
					// Ray tracing
					VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
					VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,

					// Dependencies
					VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
					VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
					VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,

					VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
				};

				VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{};
				descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

				// Enable non-uniform indexing
				descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
				descriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
				descriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
				descriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;

				VkPhysicalDeviceMaintenance4Features maintenance4Features = {
					.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES,
					.maintenance4 = VK_TRUE,
				};
				maintenance4Features.pNext = &descriptorIndexingFeatures;

				VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {};
				meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
				meshShaderFeatures.meshShader = VK_TRUE;
				meshShaderFeatures.pNext = &maintenance4Features;

				VkPhysicalDeviceBufferDeviceAddressFeatures bufferAddress{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
				.bufferDeviceAddress = VK_TRUE
				};

				bufferAddress.pNext = &meshShaderFeatures;

				VkDeviceCreateInfo deviceCreateInfo{};
				deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
				deviceCreateInfo.queueCreateInfoCount = 1;
				deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
				deviceCreateInfo.enabledExtensionCount = 10;
				deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;

				VkPhysicalDeviceRayTracingPipelineFeaturesKHR raytracingFeatures; 
				raytracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
				raytracingFeatures.rayTracingPipeline = VK_TRUE;
				raytracingFeatures.rayTracingPipelineShaderGroupHandleCaptureReplay = VK_FALSE;
				raytracingFeatures.rayTracingPipelineShaderGroupHandleCaptureReplayMixed = VK_FALSE;
				raytracingFeatures.rayTracingPipelineTraceRaysIndirect = VK_TRUE;
				raytracingFeatures.rayTraversalPrimitiveCulling = VK_FALSE;
				raytracingFeatures.pNext = &bufferAddress;

				VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures;
				accelFeatures.accelerationStructure = VK_TRUE;
				accelFeatures.accelerationStructureCaptureReplay = VK_FALSE;
				accelFeatures.accelerationStructureHostCommands = VK_FALSE;
				accelFeatures.accelerationStructureIndirectBuild = VK_FALSE;
				accelFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;
				accelFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
				accelFeatures.pNext = &raytracingFeatures;

				deviceCreateInfo.pNext = &accelFeatures;


				if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS) {
					std::cerr << "Failed to create logical device.\n";
					return 0;
				}

				return 1;
			}

			int32_t VulkanRHI::GetGraphicsQueue() {
				vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
				return 1;
			}

			int32_t VulkanRHI::GetPresentQueue() {
				vkGetDeviceQueue(device, graphicsQueueFamily, 0, &presentQueue);
				return 1;
			}

			int32_t VulkanRHI::GetSurfaceFormat() {
				uint32_t formatCount;
				vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
				std::vector<VkSurfaceFormatKHR> formats(formatCount);
				vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

				// pick 2  because it's usually VK_FORMAT_R8G8B8A8_UNORM
				surfaceFormat = formats[2];

				return 1;
			}

			int32_t VulkanRHI::CreateSwapchain() {
				swapchain = std::make_unique<Swapchain>(
					device, 
					Fox::Graphics::Managers::Vulkan::RenderPassManager::Get().GetPass(Fox::Graphics::Managers::Vulkan::RenderPass::DEFAULT)->Get(), 
					Fox::Graphics::Managers::Vulkan::TextureManager::Get().GetDepthTexture(Fox::Graphics::Managers::Vulkan::DepthTexture::DEFAULT_DEPTH_STENCIL)->GetView(),
					surface,
					surfaceFormat, 
					capabilities, 
					config.MAX_FRAMES_IN_FLIGHT);

				return 1;
			}

			int32_t VulkanRHI::CreateCommandPoolsAndAssociatedBuffersForGraphicsQueue() {
				for (uint32_t i = 0u; i < config.MAX_FRAMES_IN_FLIGHT; i++) {
					frameResources[i]->CreateGraphicsCommandResources(device, graphicsQueueFamily);
				}

				return 1;
			}

			int32_t VulkanRHI::CreateDefaultRenderPass() {
				Fox::Graphics::Managers::Vulkan::RenderPassManager::Get().Initialize(
					device,
					capabilities,
					surfaceFormat,
					depthFormat
				);

				return 1;
			}

			int32_t VulkanRHI::CreateInFlightSemaphoresAndFences() {
				for (size_t i = 0; i < config.MAX_FRAMES_IN_FLIGHT ; i++) {

					if(!frameResources[i]->CreateSynchronizationObjects(device, true))
					{
						std::cerr << "Failed to create synchronization objects for a frame!\n";
						return 0;
					}
				}

				return 1;
			}

			void VulkanRHI::Render() {

				auto& scene = Graphics::Managers::Vulkan::SceneManager::Get().GetCurrentScene();
				auto& camera = scene->GetMainCamera();

				if (inputManager->IsKeyDown(SDL_SCANCODE_A)) {
					camera->MoveLeft(cameraSpeed * deltaTime);
				}
				if (inputManager->IsKeyDown(SDL_SCANCODE_D)) {
					camera->MoveRight(cameraSpeed * deltaTime);
				}
				if (inputManager->IsKeyDown(SDL_SCANCODE_W)) {
					camera->MoveForward(cameraSpeed * deltaTime);
				}
				if (inputManager->IsKeyDown(SDL_SCANCODE_S)) {
					camera->MoveBackward(cameraSpeed * deltaTime);
				}
				if (inputManager->IsKeyDown(SDL_SCANCODE_R)) {
					camera->MoveUp(cameraSpeed * deltaTime);
				}
				if (inputManager->IsKeyDown(SDL_SCANCODE_F)) {
					camera->MoveDown(cameraSpeed * deltaTime);
				}

				currentFrame = (currentFrameIndex) % config.MAX_FRAMES_IN_FLIGHT;
				auto frameResource = frameResources[currentFrame].get();

				// 2. STALL THE CPU right here if the GPU hasn't cleared this specific slot's workload yet
				frameResource->renderFence->Wait();
				// Do not reset the fence yet! Wait until after swapchain acquisition succeeds.

				// 3. Acquire the image index safely
				uint32_t imageIndex = swapchain->AcquireNextImage(frameResource->imageAvailableSemaphore->Get());

				currentImageIndex = imageIndex;

				// 4. NOW it is safe to reset the fence and clear out command buffers
				frameResource->renderFence->Reset();
				frameResource->commandPool->Reset();
				
				UpdateUniformBuffer(currentFrame);

				VkImageSubresourceRange subresourceRange = {
						VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
				};

				auto& raytracingPipeline = Fox::Graphics::Managers::Vulkan::PipelineManager::Get().GetRayTracingPipeline(Fox::Graphics::Managers::Vulkan::RayTracingPipelineCategory::MAIN_RAYTRACING_PIPELINE);
				auto raytracingPipelineLayout = Fox::Graphics::Managers::Vulkan::PipelineManager::Get().GetRayTracingPipelineLayout(Fox::Graphics::Managers::Vulkan::RayTracingPipelineCategory::MAIN_RAYTRACING_PIPELINE)->Get();

				frameResource->commandList->Begin()
					.SetRecursionStackSize(raytracingPipeline->GetPipeline())
					.SetViewport(0, 0, capabilities.currentExtent.width, capabilities.currentExtent.height)
					.SetScissor(0, 0, capabilities.currentExtent.width, capabilities.currentExtent.height)
					.BindPipeline(raytracingPipeline->GetPipeline(), VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
					.BindDescriptorSets(raytracingPipelineLayout, 0, { frameResource->perFrameDescriptorSet->Get() }, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)

					.Raytrace(
						raytracingPipeline->GetRaygenRegion(),
						raytracingPipeline->GetMissRegion(),
						raytracingPipeline->GetHitRegion(),
						raytracingPipeline->GetCallableRegion(),
						capabilities.currentExtent.width, capabilities.currentExtent.height)

					// 1. Wait for Ray Tracing writes to finish before reading as a Transfer Source
					.TransitionImageLayout(
						frameResource->storageTexture->GetImage(),
						VkImageLayout::VK_IMAGE_LAYOUT_GENERAL,
						VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						subresourceRange,
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT)

					// 2. Treat the freshly acquired swapchain image as UNDEFINED to prevent synchronization faults
					.TransitionImageLayout(
						swapchain->GetImage(imageIndex),
						VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED, // Changed from PRESENT_SRC
						VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						subresourceRange,
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT)

					.CopyImage(
						frameResource->storageTexture->GetImage(),
						swapchain->GetImage(imageIndex),
						capabilities.currentExtent.width,
						capabilities.currentExtent.height)

					// 3. Ready swapchain image for presentation
					.TransitionImageLayout(
						swapchain->GetImage(imageIndex),
						VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
						subresourceRange,
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT,
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)

					// 4. Return storage texture safely back to General layout for the next frame's Raytrace pass
					.TransitionImageLayout(
						frameResource->storageTexture->GetImage(),
						VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						VkImageLayout::VK_IMAGE_LAYOUT_GENERAL,
						subresourceRange,
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT,
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR)
					.End()
					.Submit(graphicsQueue, frameResource->imageAvailableSemaphore->Get(), frameResource->renderFinishedSemaphore->Get(), frameResource->renderFence->Get());

				Present(swapchain, imageIndex, frameResource->renderFinishedSemaphore);

				currentFrameIndex++;
			}

			const char* GetDebugSeverity(VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
				switch (severity)
				{
				case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
					return "Verbose";

				case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
					return "Info";

				case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
					return "Warning";

				case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
					return "Error";

				default:
					std::cout << "Invalid severity code" << severity << std::endl;
					exit(1);
				}

				return "No such severity";
			}

			void VulkanRHI::Present(const std::unique_ptr<Fox::Graphics::Vulkan::Swapchain>& swapchain,
				uint32_t imageIndex, 
				std::unique_ptr<Fox::Graphics::Vulkan::Semaphore>& semaphore) {
				VkSemaphore signalSemaphores[] = { semaphore->Get()};

				VkSwapchainKHR swapchainKHR = swapchain->Get();
				VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
				presentInfo.waitSemaphoreCount = 1;
				presentInfo.pWaitSemaphores = signalSemaphores;
				presentInfo.swapchainCount = 1;
				presentInfo.pSwapchains = &swapchainKHR;
				presentInfo.pImageIndices = &imageIndex;

				vkQueuePresentKHR(presentQueue, &presentInfo);
			}

			const char* GetDebugType(VkDebugUtilsMessageTypeFlagsEXT type) {
				switch (type)
				{
				case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
					return "General";

				case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
					return "Validation";

				case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
					return "Performance";

				case VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT:
					return "Device address binding";

				default:
					std::cout << "Invalid type code" << type << std::endl;;
					exit(1);
				}

				return "No such type";
			}

			static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
				VkDebugUtilsMessageSeverityFlagBitsEXT severity,
				VkDebugUtilsMessageTypeFlagsEXT type,
				const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
				void* pUserData) {
				std::cout << "Debug callback: " << pCallbackData->pMessage << std::endl;
				std::cout << "Severity: " << GetDebugSeverity(severity) << std::endl;
				std::cout << "  Type " << GetDebugType(type) << std::endl;
				std::cout << "  Objects " << std::endl;

				for (uint32_t i = 0; i < pCallbackData->objectCount; i++) {
					printf("%llx ", pCallbackData->pObjects[i].objectHandle);
				}

				return VK_FALSE;
			}

			void VulkanRHI::CreateDebugCallback() {
				VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo = {
					.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
					.pNext = NULL,
					.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
									   VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
									   VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
									   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
					.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
								   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
								   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
					.pfnUserCallback = &DebugCallback,
					.pUserData = NULL
				};

				PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessenger = VK_NULL_HANDLE;
				vkCreateDebugUtilsMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

				if (!vkCreateDebugUtilsMessenger) {
					std::cout << "Cannot find address of vkCreateDebugUtilsMessenger" << std::endl;
					exit(1);
				}

				VkResult result = vkCreateDebugUtilsMessenger(instance, &messengerCreateInfo, NULL, &debugMessenger);
				if (result != VK_SUCCESS) {
					std::cout << "Creating debug utils messenger failed." << std::endl;
				}

				std::cout << "Debug utils messenger created." << std::endl;
			}

			void VulkanRHI::DestroyDebugCallback() {
				PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessenger = VK_NULL_HANDLE;
				vkDestroyDebugUtilsMessenger = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");

				if (!vkDestroyDebugUtilsMessenger) {
					std::cout << "Cannot find address of vkDestroyDebugUtilsMessenger" << std::endl;
					exit(1);
				}

				vkDestroyDebugUtilsMessenger(instance, debugMessenger, NULL);

				std::cout << "Debug utils messenger destroyed." << std::endl;
			}

			int32_t VulkanRHI::InitializePipelines() {
				Fox::Graphics::Managers::Vulkan::PipelineManager::Get().Initialize(device, physicalDevice, capabilities);
				std::cout << "Graphics pipeline successfully created" << std::endl;
				return 1;
			}

			void VulkanRHI::LoadExtensionAPIFunctions() {
				Fox::Graphics::Vulkan::CommandList::RegisterRenderMeshShaderFunction(
					reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(vkGetDeviceProcAddr(device, "vkCmdDrawMeshTasksEXT")),
					reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR")));
				Fox::Graphics::Vulkan::CommandList::RegisterObjectNameFunction(
					reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT")));
				Fox::Graphics::Vulkan::CommandList::RegisterGetObjectName(reinterpret_cast<PFN_vkDebugMarkerSetObjectNameEXT>(vkGetDeviceProcAddr(device, "vkGetObjectNameEXT")));
				Fox::Graphics::Vulkan::CommandList::RegisterRayTracingStackSizeFunctions(
					reinterpret_cast<PFN_vkGetRayTracingShaderGroupStackSizeKHR>(vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupStackSizeKHR")),
						reinterpret_cast<PFN_vkCmdSetRayTracingPipelineStackSizeKHR>(vkGetDeviceProcAddr(device, "vkCmdSetRayTracingPipelineStackSizeKHR")));
			}

			int32_t VulkanRHI::CreateUniformBuffers() {

				auto& scene = Fox::Graphics::Managers::Vulkan::SceneManager::Get().GetCurrentScene();

				for (uint32_t i = 0; i < config.MAX_FRAMES_IN_FLIGHT; i++) {
					frameResources[i]->CreateConstantBuffers(device, physicalDevice);
				}

				Fox::Graphics::Managers::Vulkan::DescriptorManager& descriptorManager =
					Fox::Graphics::Managers::Vulkan::DescriptorManager::Get();

				descriptorManager.Initialize(device, config);

				for(auto& frameResource : frameResources)
				{
					frameResource->perFrameDescriptorSet = std::make_unique<Fox::Graphics::Vulkan::DescriptorSet>(descriptorManager.GetDescriptorSet(Fox::Graphics::Managers::Vulkan::Descriptor::RAY_TRACING)->AllocateSet()); //({ 100 })); // 100 meshes in raytracing scene at max
					frameResource->offscreenDescriptorSet = std::make_unique<Fox::Graphics::Vulkan::DescriptorSet>(descriptorManager.GetDescriptorSet(Fox::Graphics::Managers::Vulkan::Descriptor::OFFSCREEN)->AllocateSet());

					frameResource->perFrameDescriptorSet->Reserve(7);

					frameResource->meshInfosUBO->Update(scene->GetSceneMeshInfos());

					Fox::Graphics::Vulkan::MeshTransforms meshTransforms{};
					meshTransforms.models.push_back(glm::mat4(1.0f));
						
					meshTransforms.models.push_back(glm::rotate(glm::mat4(1.0f),
						(float)rotationAngle * glm::radians(45.0f),
						glm::vec3(0.0f, 0.0f, 1.0f)) * glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f)));

					meshTransforms.models.push_back(glm::rotate(glm::mat4(1.0f),
						(float)rotationAngle * glm::radians(60.0f),
						glm::vec3(0.0f, 0.0f, 1.0f)) * glm::translate(glm::mat4(1.0f), glm::vec3(8.0f, 0.0f, 0.0f)));

					frameResource->meshTransformsUBO->Update(meshTransforms.models);

					Fox::Graphics::Vulkan::CameraUBO cam;
					cam.camPos = scene->GetMainCamera()->GetPosition(); //glm::vec3(0.0f);
					cam.camForward = scene->GetMainCamera()->GetForward();
					cam.camRight = scene->GetMainCamera()->GetRight();
					cam.camUp = scene->GetMainCamera()->GetUp();
					cam.resolution = glm::vec2(capabilities.currentExtent.width, capabilities.currentExtent.height);
					cam.frameIndex = currentFrameIndex;

					frameResource->camUBO->Update(cam);
				}

				for (uint32_t i = 0; i < config.MAX_FRAMES_IN_FLIGHT; i++) {

					frameResources[i]->perFrameDescriptorSet->ClearWrites();
					auto tlas = raytracingScene->GetTLAS();
					frameResources[i]->perFrameDescriptorSet->SetAccelerationStructure(0, tlas);
					frameResources[i]->perFrameDescriptorSet->SetStorageImage(1, frameResources[i]->storageTexture->GetView());
					frameResources[i]->perFrameDescriptorSet->SetConstantBuffer(2, frameResources[i]->camUBO);
					frameResources[i]->perFrameDescriptorSet->SetStorageBuffer(3, raytracingScene->vertexSSBO->GetBufferUnique());
					frameResources[i]->perFrameDescriptorSet->SetStorageBuffer(4, raytracingScene->indexSSBO->GetBufferUnique());
					frameResources[i]->perFrameDescriptorSet->SetStorageBuffer(5, raytracingScene->submeshSSBO->GetBufferUnique());
					frameResources[i]->perFrameDescriptorSet->SetStorageBuffer(6, raytracingScene->materialsSSBO->GetBufferUnique());
					frameResources[i]->perFrameDescriptorSet->SetStorageBuffer(7, raytracingScene->lightsSSBO->GetBufferUnique()); 
					frameResources[i]->perFrameDescriptorSet->Update();  
				}



				return 1;
			}

			uint32_t VulkanRHI::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
				VkPhysicalDeviceMemoryProperties memProperties;
				vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

				for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
					if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
						return i;
					}
				}

				throw std::runtime_error("Failed to find suitable memory type!");
			}

			void VulkanRHI::UpdateUniformBuffer(uint32_t currentFrame) {
				auto& scene = Graphics::Managers::Vulkan::SceneManager::Get().GetCurrentScene();
				auto& camera = scene->GetMainCamera();

				Fox::Graphics::Vulkan::CameraUBO cam;
				cam.camPos = camera->GetPosition();
				cam.camForward = camera->GetForward();
				cam.camRight = camera->GetRight();
				cam.camUp = camera->GetUp();
				cam.resolution = glm::vec2(capabilities.currentExtent.width, capabilities.currentExtent.height);
				cam.frameIndex = currentFrameIndex;

				frameResources[currentFrame]->camUBO->Update(cam);
			}

			void VulkanRHI::RegisterInput(Fox::Input::InputManager& input) {
				inputManager = &input;

				input.ToggleRelativeMode();

				float sensitivity = 0.13f;

				mouseInputBind = input.OnMouseMoved.Connect([=](int dx, int dy) {

					if (input.IsRelativeMode()) {
						Fox::Graphics::Managers::Vulkan::SceneManager::Get().GetCurrentScene()->GetMainCamera()->Rotate(static_cast<float>(dx) * sensitivity, -static_cast<float>(dy) * sensitivity);
					}
				});

			}

		}
	}
}