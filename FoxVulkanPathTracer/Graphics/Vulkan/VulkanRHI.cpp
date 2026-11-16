#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace Managers = Fox::Graphics::Managers::Vulkan;

			namespace {

				// Everything the renderer cannot run without. Devices missing any of these are skipped.
				const std::vector<const char*> REQUIRED_DEVICE_EXTENSIONS = {
					VK_KHR_SWAPCHAIN_EXTENSION_NAME,
					VK_EXT_MESH_SHADER_EXTENSION_NAME,

					// Ray tracing
					VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
					VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
					VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
				};

				// Van der Corput radical inverse, the building block of the Halton sequence
				float RadicalInverse(uint32_t index, uint32_t base) {
					float result = 0.0f;
					float fraction = 1.0f / static_cast<float>(base);
					while (index > 0) {
						result += static_cast<float>(index % base) * fraction;
						index /= base;
						fraction /= static_cast<float>(base);
					}
					return result;
				}

				// Adds the optional extensions that exist (and are not listed yet) to enabled.
				// The strings in optional must outlive the use of enabled.
				void AppendSupportedExtensions(std::vector<const char*>& enabled, const std::vector<std::string>& optional, const std::vector<VkExtensionProperties>& available) {
					for (const std::string& name : optional) {
						const bool isAvailable = std::any_of(available.begin(), available.end(),
							[&name](const VkExtensionProperties& extension) { return name == extension.extensionName; });

						const bool isEnabled = std::any_of(enabled.begin(), enabled.end(),
							[&name](const char* extension) { return name == extension; });

						if (isAvailable && !isEnabled) {
							enabled.push_back(name.c_str());
						}
					}
				}

				bool SupportsExtensions(VkPhysicalDevice physicalDevice, const std::vector<const char*>& required) {
					uint32_t count = 0;
					vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
					std::vector<VkExtensionProperties> available(count);
					vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, available.data());

					for (const char* name : required) {
						const bool found = std::any_of(available.begin(), available.end(),
							[name](const VkExtensionProperties& ext) { return strcmp(ext.extensionName, name) == 0; });

						if (!found) {
							return false;
						}
					}

					return true;
				}

				VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
					VkDebugUtilsMessageSeverityFlagBitsEXT severity,
					VkDebugUtilsMessageTypeFlagsEXT type,
					const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
					void* userData) {

					// NVIDIA NGX (DLSS) uses some of its own internal images before giving them a layout, right
					// after a feature is created. The images are named by NGX; nothing the renderer can change.
					if (strstr(callbackData->pMessage, "[nv.ngx.") != nullptr) {
						return VK_FALSE;
					}

					const char* severityName = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" : "WARNING";

					// type is a bitmask, several bits may be set at once
					const char* typeName = "General";
					if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) typeName = "Validation";
					else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) typeName = "Performance";

					std::cerr << "[Vulkan " << severityName << " / " << typeName << "] " << callbackData->pMessage << "\n";

					return VK_FALSE;
				}
			}

			VulkanRHI::~VulkanRHI() {
				Destroy();
			}

			// ------------------------------------------------------------------------------------
			// Initialization
			// ------------------------------------------------------------------------------------

			bool VulkanRHI::Initialize(const Fox::Graphics::RendererConfig& config) {
				this->config = config;

				if (!CreateInstance()) {
					return false;
				}

#ifdef _DEBUG
				CreateDebugCallback();
#endif

				if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(config.windowHandle), instance, &surface)) {
					std::cerr << "SDL_Vulkan_CreateSurface failed: " << SDL_GetError() << "\n";
					return false;
				}

				if (!PickPhysicalDevice()) {
					std::cerr << "No GPU with hardware ray tracing and mesh shader support was found.\n";
					return false;
				}

				if (!FindGraphicsQueueFamily()) {
					std::cerr << "Failed to find a queue family that supports graphics and presentation.\n";
					return false;
				}

				if (!CreateLogicalDevice()) {
					std::cerr << "Failed to create the logical device.\n";
					return false;
				}

				LoadExtensionAPIFunctions();

				// One family serves both roles (see FindGraphicsQueueFamily)
				vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
				presentQueue = graphicsQueue;

				if (!ChooseSurfaceFormat() || !ChooseDepthFormat()) {
					std::cerr << "Failed to find suitable surface / depth formats.\n";
					return false;
				}

				vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

				Managers::RenderPassManager::Get().Initialize(device, capabilities, surfaceFormat, depthFormat);

				frameResources.resize(config.MAX_FRAMES_IN_FLIGHT);
				for (uint32_t i = 0; i < config.MAX_FRAMES_IN_FLIGHT; i++) {
					frameResources[i] = std::make_unique<FrameResource>();
					frameResources[i]->frameIndex = i;
					frameResources[i]->CreateSynchronizationObjects(device, true);
					frameResources[i]->CreateGraphicsCommandResources(device, graphicsQueueFamily);
					frameResources[i]->CreateConstantBuffers(device, physicalDevice);
				}

				VkCommandPool setupCommandPool = frameResources[0]->commandPool->Get();

				Managers::TextureManager::Get().Initialize(device, physicalDevice, capabilities.currentExtent, depthFormat, graphicsQueue, setupCommandPool);

				CreateSwapchain();

				Managers::MeshManager::Get().Initialize(device);
				Managers::SceneManager::Get().Initialize(device, physicalDevice, setupCommandPool, graphicsQueue, capabilities);

				RayTracing::RayTracingPipeline::RegisterExtensionFunctions(device);
				Fox::Scene::RayTracing::RayTracingScene::RegisterExtensionFunctions(device);

				raytracingScene = std::make_unique<Fox::Scene::RayTracing::MainRayTracingScene>(device, physicalDevice, graphicsQueue, graphicsQueueFamily);

				deviceContext.instance = instance;
				deviceContext.physicalDevice = physicalDevice;
				deviceContext.device = device;
				deviceContext.graphicsQueue = graphicsQueue;
				deviceContext.graphicsQueueFamily = graphicsQueueFamily;
				deviceContext.setupCommandPool = setupCommandPool;

				CreateGBuffer();
				denoisers = RayTracing::CreateDenoisers(deviceContext);

				Managers::DescriptorManager::Get().Initialize(device, config);
				CreateFrameDescriptors();

				Managers::PipelineManager::Get().Initialize(device, physicalDevice, capabilities);

				postProcessor = std::make_unique<PostProcessor>(device);
				BindGBuffer();

				std::cout << "Vulkan renderer initialized (" << capabilities.currentExtent.width << "x" << capabilities.currentExtent.height << ").\n";
				return true;
			}

			void VulkanRHI::Destroy() {
				if (device != VK_NULL_HANDLE) {
					vkDeviceWaitIdle(device);

					mouseInputBind.Disconnect();
					keyInputBind.Disconnect();

					for (auto& frameResource : frameResources) {
						frameResource->Destroy(device);
					}
					frameResources.clear();

					postProcessor = nullptr;
					renderFinishedSemaphores.clear();
					denoisers.clear();
					gbuffer = nullptr;
					swapchain = nullptr;
					raytracingScene = nullptr;

					Managers::RenderPassManager::Get().Destroy();
					Managers::PipelineManager::Get().Destroy();
					Managers::TextureManager::Get().Destroy();
					Managers::DescriptorManager::Get().Destroy();
					Managers::MeshManager::Get().Destroy();
					Managers::SceneManager::Get().Destroy();

					vkDestroyDevice(device, nullptr);
					device = VK_NULL_HANDLE;
				}

				if (instance != VK_NULL_HANDLE) {
					if (surface != VK_NULL_HANDLE) {
						vkDestroySurfaceKHR(instance, surface, nullptr);
						surface = VK_NULL_HANDLE;
					}

					DestroyDebugCallback();

					vkDestroyInstance(instance, nullptr);
					instance = VK_NULL_HANDLE;
				}
			}

			bool VulkanRHI::CreateInstance() {
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
				// Extensions the window system needs, plus whatever the denoisers can make use of
				std::vector<const char*> extensions = config.instanceExtensions;

				uint32_t availableCount = 0;
				vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
				std::vector<VkExtensionProperties> available(availableCount);
				vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());

				optionalInstanceExtensions = RayTracing::GetDenoiserInstanceExtensions();
				AppendSupportedExtensions(extensions, optionalInstanceExtensions, available);

				instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
				instanceCreateInfo.ppEnabledExtensionNames = extensions.data();
				instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(config.layers.size());
				instanceCreateInfo.ppEnabledLayerNames = config.layers.data();

				VkResult result = vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
				if (result != VK_SUCCESS) {
					std::cerr << "vkCreateInstance failed: " << result << "\n";
					return false;
				}

				return true;
			}

			bool VulkanRHI::PickPhysicalDevice() {
				uint32_t gpuCount = 0;
				vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
				std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
				vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data());

				// Take the first device that can run the renderer, but prefer a discrete GPU:
				// on laptops the integrated GPU is frequently enumerated first.
				for (VkPhysicalDevice candidate : physicalDevices) {
					if (!SupportsExtensions(candidate, REQUIRED_DEVICE_EXTENSIONS)) {
						continue;
					}

					VkPhysicalDeviceProperties properties{};
					vkGetPhysicalDeviceProperties(candidate, &properties);

					const bool isDiscrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
					if (physicalDevice == VK_NULL_HANDLE || isDiscrete) {
						physicalDevice = candidate;
						std::cout << "Using GPU: " << properties.deviceName << "\n";
					}

					if (isDiscrete) {
						break;
					}
				}

				return physicalDevice != VK_NULL_HANDLE;
			}

			bool VulkanRHI::FindGraphicsQueueFamily() {
				uint32_t queueFamilyCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
				std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
				vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

				for (uint32_t i = 0; i < queueFamilyCount; ++i) {
					VkBool32 presentSupport = VK_FALSE;
					vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);

					if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
						graphicsQueueFamily = i;
						return true;
					}
				}

				return false;
			}

			bool VulkanRHI::CreateLogicalDevice() {
				float queuePriority = 1.0f;
				VkDeviceQueueCreateInfo queueCreateInfo{};
				queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueCreateInfo.queueFamilyIndex = graphicsQueueFamily;
				queueCreateInfo.queueCount = 1;
				queueCreateInfo.pQueuePriorities = &queuePriority;

				// Feature chain. Everything here is used by the shaders or the acceleration structure code.
				// Features that were promoted to core are enabled through the Vulkan 1.x feature structs,
				// which is why the matching extensions are not listed in REQUIRED_DEVICE_EXTENSIONS.
				VkPhysicalDeviceRayTracingPipelineFeaturesKHR raytracingFeatures{};
				raytracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
				raytracingFeatures.rayTracingPipeline = VK_TRUE;

				VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures{};
				accelFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
				accelFeatures.pNext = &raytracingFeatures;
				accelFeatures.accelerationStructure = VK_TRUE;

				VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
				meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
				meshShaderFeatures.pNext = &accelFeatures;
				meshShaderFeatures.meshShader = VK_TRUE; // Full-screen quad of the tone mapping pass

				VkPhysicalDeviceVulkan13Features vulkan13Features{};
				vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
				vulkan13Features.pNext = &meshShaderFeatures;
				vulkan13Features.maintenance4 = VK_TRUE;

				VkPhysicalDeviceVulkan12Features vulkan12Features{};
				vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
				vulkan12Features.pNext = &vulkan13Features;
				vulkan12Features.bufferDeviceAddress = VK_TRUE;
				vulkan12Features.shaderFloat16 = VK_TRUE; // The ray payload stores half precision colours
				// Bindless texture array
				vulkan12Features.descriptorIndexing = VK_TRUE;
				vulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
				vulkan12Features.runtimeDescriptorArray = VK_TRUE;
				vulkan12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;
				vulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;

				VkDeviceCreateInfo deviceCreateInfo{};
				deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
				deviceCreateInfo.pNext = &vulkan12Features;
				deviceCreateInfo.queueCreateInfoCount = 1;
				deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
				// Required extensions, plus whatever the denoisers can make use of
				std::vector<const char*> extensions = REQUIRED_DEVICE_EXTENSIONS;

				uint32_t availableCount = 0;
				vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &availableCount, nullptr);
				std::vector<VkExtensionProperties> available(availableCount);
				vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &availableCount, available.data());

				optionalDeviceExtensions = RayTracing::GetDenoiserDeviceExtensions(instance, physicalDevice);
				AppendSupportedExtensions(extensions, optionalDeviceExtensions, available);

				deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
				deviceCreateInfo.ppEnabledExtensionNames = extensions.data();

				return vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) == VK_SUCCESS;
			}

			bool VulkanRHI::ChooseSurfaceFormat() {
				uint32_t formatCount = 0;
				vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
				if (formatCount == 0) {
					return false;
				}

				std::vector<VkSurfaceFormatKHR> formats(formatCount);
				vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

				// The tone mapping shader applies the sRGB transfer curve itself, so pick a UNORM
				// format: an _SRGB one would encode the output a second time.
				surfaceFormat = formats[0];
				for (const VkSurfaceFormatKHR& candidate : formats) {
					const bool isUnorm = candidate.format == VK_FORMAT_B8G8R8A8_UNORM || candidate.format == VK_FORMAT_R8G8B8A8_UNORM;

					if (isUnorm && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
						surfaceFormat = candidate;
						return true;
					}
				}

				std::cerr << "No 8-bit UNORM surface format available, colours may look washed out.\n";
				return true;
			}

			bool VulkanRHI::ChooseDepthFormat() {
				for (VkFormat candidate : { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT }) {
					VkFormatProperties properties{};
					vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate, &properties);

					if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
						depthFormat = candidate;
						return true;
					}
				}

				return false;
			}

			void VulkanRHI::LoadExtensionAPIFunctions() {
				CommandList::RegisterRenderMeshShaderFunction(
					reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(vkGetDeviceProcAddr(device, "vkCmdDrawMeshTasksEXT")),
					reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR")));

#ifdef _DEBUG
				// Only available when VK_EXT_debug_utils is enabled on the instance
				CommandList::RegisterObjectNameFunction(
					reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT")));
#endif
			}

			void VulkanRHI::CreateDebugCallback() {
				VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo{};
				messengerCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
				messengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
													  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
				messengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
												  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
												  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
				messengerCreateInfo.pfnUserCallback = &DebugCallback;

				auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

				if (!createMessenger || createMessenger(instance, &messengerCreateInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
					std::cerr << "Could not create the Vulkan debug messenger, validation output is disabled.\n";
					debugMessenger = VK_NULL_HANDLE;
				}
			}

			void VulkanRHI::DestroyDebugCallback() {
				if (debugMessenger == VK_NULL_HANDLE) {
					return;
				}

				auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
				if (destroyMessenger) {
					destroyMessenger(instance, debugMessenger, nullptr);
				}

				debugMessenger = VK_NULL_HANDLE;
			}

			// ------------------------------------------------------------------------------------
			// Resources
			// ------------------------------------------------------------------------------------

			void VulkanRHI::CreateSwapchain() {
				swapchain = std::make_unique<Swapchain>(
					device,
					Managers::RenderPassManager::Get().GetPass(Managers::RenderPass::DEFAULT)->Get(),
					Managers::TextureManager::Get().GetDepthTexture(Managers::DepthTexture::DEFAULT_DEPTH_STENCIL)->GetView(),
					surface,
					surfaceFormat,
					capabilities,
					config.MAX_FRAMES_IN_FLIGHT);

				renderFinishedSemaphores.clear();
				for (uint32_t i = 0; i < swapchain->GetImageCount(); i++) {
					renderFinishedSemaphores.push_back(std::make_unique<Semaphore>(device));
				}
			}

			void VulkanRHI::CreateGBuffer() {
				gbuffer = std::make_unique<RayTracing::GBuffer>(deviceContext, capabilities.currentExtent);
				ResetHistory();
			}

			void VulkanRHI::CreateFrameDescriptors() {
				auto& descriptorManager = Managers::DescriptorManager::Get();
				auto& textureManager = Managers::TextureManager::Get();

				// Set 0: scene data, one copy per frame in flight because the camera buffer changes every frame.
				VkAccelerationStructureKHR tlas = raytracingScene->GetTLAS();

				for (auto& frameResource : frameResources) {
					frameResource->perFrameDescriptorSet = std::make_unique<DescriptorSet>(
						descriptorManager.GetDescriptorSet(Managers::Descriptor::RAY_TRACING)->AllocateSet());

					auto& set = frameResource->perFrameDescriptorSet;
					set->Reserve(13);
					set->SetAccelerationStructure(0, tlas);
					set->SetStorageImage(1, gbuffer->radiance->GetView());
					set->SetStorageImage(8, gbuffer->albedo->GetView());
					set->SetStorageImage(9, gbuffer->normalDepth->GetView());
					set->SetStorageImage(10, gbuffer->motion->GetView());
					set->SetStorageImage(11, gbuffer->specular->GetView());
					set->SetStorageBuffer(12, textureManager.environmentSamplingTable->GetBufferUnique());
					set->SetConstantBuffer(2, frameResource->camUBO);
					set->SetStorageBuffer(3, raytracingScene->vertexSSBO->GetBufferUnique());
					set->SetStorageBuffer(4, raytracingScene->indexSSBO->GetBufferUnique());
					set->SetStorageBuffer(5, raytracingScene->submeshSSBO->GetBufferUnique());
					set->SetStorageBuffer(6, raytracingScene->materialsSSBO->GetBufferUnique());
					set->SetStorageBuffer(7, raytracingScene->lightsSSBO->GetBufferUnique());
					set->Update();
				}

				// Set 1: environment settings + every texture referenced by the scene's materials.
				textureManager.bindlessTextureDescriptorSet = std::make_unique<DescriptorSet>(
					descriptorManager.GetDescriptorSet(Managers::Descriptor::BINDLESS_TEXTURES)->AllocateSet({ Managers::TextureManager::MAX_BINDLESS_TEXTURES }));

				auto& bindlessSet = textureManager.bindlessTextureDescriptorSet;

				uint32_t textureIndex = 0;
				for (const auto& texture : textureManager.GetBindlessTextureArray()) {
					bindlessSet->SetBindlessTexture(1, textureIndex++, texture.get());
				}

				bindlessSet->SetConstantBuffer(0, textureManager.environmentUBO);
				bindlessSet->Update();

				textureManager.UpdateEnvironment();
			}

			void VulkanRHI::BindGBuffer() {
				for (auto& frameResource : frameResources) {
					auto& set = frameResource->perFrameDescriptorSet;
					set->ClearWrites();
					set->Reserve(5);
					set->SetStorageImage(1, gbuffer->radiance->GetView());
					set->SetStorageImage(8, gbuffer->albedo->GetView());
					set->SetStorageImage(9, gbuffer->normalDepth->GetView());
					set->SetStorageImage(10, gbuffer->motion->GetView());
					set->SetStorageImage(11, gbuffer->specular->GetView());
					set->Update();
				}

				for (auto& denoiser : denoisers) {
					denoiser->Resize(*gbuffer);
				}

				SelectDenoiser(activeDenoiser);
			}

			void VulkanRHI::SelectDenoiser(size_t index) {
				// The tone mapping descriptor set is about to change, no frame may still be using it
				vkDeviceWaitIdle(device);

				activeDenoiser = index % denoisers.size();
				RayTracing::Denoiser& denoiser = *denoisers[activeDenoiser];

				postProcessor->SetInput(denoiser.GetOutputView(), VK_IMAGE_LAYOUT_GENERAL);
				ResetHistory();

				std::cout << "Denoiser: " << denoiser.GetName() << "\n";
				UpdateWindowTitle(0.0f);
			}

			void VulkanRHI::UpdateWindowTitle(float deltaTime) {
				// Average the frame rate over half a second so the number is readable
				titleTimer += deltaTime;
				titleFrames++;

				if (deltaTime > 0.0f && titleTimer < 0.5f) {
					return;
				}

				std::ostringstream title;
				title << "Fox Renderer - " << denoisers[activeDenoiser]->GetName() << " - " << samplesPerPixel << " spp";
				if (titleTimer > 0.0f) {
					title << " - " << static_cast<int>(std::round(titleFrames / titleTimer)) << " fps";
				}

				SDL_SetWindowTitle(static_cast<SDL_Window*>(config.windowHandle), title.str().c_str());

				titleTimer = 0.0f;
				titleFrames = 0;
			}

			glm::vec2 VulkanRHI::NextJitter() const {
				switch (denoisers[activeDenoiser]->GetJitterMode()) {
				case RayTracing::JitterMode::Random:
					// Scrambled Halton: effectively random, but covers the pixel evenly over time
					return glm::vec2(RadicalInverse(frameCounter + 1, 2), RadicalInverse(frameCounter + 1, 3)) - 0.5f;
				case RayTracing::JitterMode::Halton:
					// Short repeating sequence, as temporal upscalers expect
					return glm::vec2(RadicalInverse(frameCounter % 32 + 1, 2), RadicalInverse(frameCounter % 32 + 1, 3)) - 0.5f;
				default:
					return glm::vec2(0.0f);
				}
			}

			// ------------------------------------------------------------------------------------
			// Frame
			// ------------------------------------------------------------------------------------

			void VulkanRHI::Render(float deltaTime) {
				// Nothing to draw into while the window is minimized
				if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0) {
					return;
				}

				HandleCameraInput(deltaTime);
				UpdateWindowTitle(deltaTime);

				FrameResource& frameResource = *frameResources[currentFrame];

				// Block until the GPU has finished the work that last used this frame slot
				frameResource.renderFence->Wait();

				uint32_t imageIndex = 0;
				VkResult acquireResult = swapchain->AcquireNextImage(frameResource.imageAvailableSemaphore->Get(), imageIndex);
				if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
					Resize(0, 0);
					return;
				}

				// Only reset once we know a submit will follow, otherwise the next Wait() would hang
				frameResource.renderFence->Reset();
				frameResource.commandPool->Reset();

				UpdateCameraUniforms(frameResource, deltaTime);
				RecordFrame(frameResource, imageIndex);

				const VkSemaphore renderFinished = renderFinishedSemaphores[imageIndex]->Get();

				frameResource.commandList->Submit(
					graphicsQueue,
					frameResource.imageAvailableSemaphore->Get(),
					renderFinished,
					frameResource.renderFence->Get());

				VkResult presentResult = Present(imageIndex, renderFinished);

				currentFrame = (currentFrame + 1) % config.MAX_FRAMES_IN_FLIGHT;
				frameCounter++;

				if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
					Resize(0, 0);
				}
			}

			void VulkanRHI::HandleCameraInput(float deltaTime) {
				if (!inputManager) {
					return;
				}

				auto& camera = Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera();
				const float distance = cameraSpeed * deltaTime;

				if (inputManager->IsKeyDown(SDL_SCANCODE_A)) camera->MoveLeft(distance);
				if (inputManager->IsKeyDown(SDL_SCANCODE_D)) camera->MoveRight(distance);
				if (inputManager->IsKeyDown(SDL_SCANCODE_W)) camera->MoveForward(distance);
				if (inputManager->IsKeyDown(SDL_SCANCODE_S)) camera->MoveBackward(distance);
				if (inputManager->IsKeyDown(SDL_SCANCODE_R)) camera->MoveUp(distance);
				if (inputManager->IsKeyDown(SDL_SCANCODE_F)) camera->MoveDown(distance);
			}

			void VulkanRHI::UpdateCameraUniforms(FrameResource& frameResource, float deltaTime) {
				auto& camera = Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera();

				CameraState current{};
				current.position = camera->GetPosition();
				current.forward = camera->GetForward();
				current.right = camera->GetRight();
				current.up = camera->GetUp();

				// Without usable history the previous camera is the current one: zero motion
				const CameraState previous = resetHistory ? current : previousCamera;

				const VkExtent2D extent = capabilities.currentExtent;
				const glm::vec2 jitter = NextJitter();

				CameraUBO cam{};
				cam.camPos = current.position;
				cam.camForward = current.forward;
				cam.camRight = current.right;
				cam.camUp = current.up;
				cam.prevCamPos = previous.position;
				cam.prevCamForward = previous.forward;
				cam.prevCamRight = previous.right;
				cam.prevCamUp = previous.up;
				cam.resolution = glm::vec2(extent.width, extent.height);
				cam.jitter = jitter;
				cam.frameIndex = frameCounter;
				cam.lightCount = raytracingScene->GetLightCount();
				cam.tanHalfFov = std::tan(camera->GetFov() * 0.5f);
				cam.samplesPerPixel = samplesPerPixel;
				// Temporal upscalers need every path of a pixel at the reported sub-pixel position
				cam.jitterAllSamples = denoisers[activeDenoiser]->GetJitterMode() == RayTracing::JitterMode::Halton ? 1u : 0u;
				cam.gbufferFollowsReflections = denoisers[activeDenoiser]->WantsReflectedSurfaces() ? 1u : 0u;

				frameResource.camUBO->Update(cam);

				// What the denoiser needs to know about this frame
				denoiserFrame = RayTracing::DenoiserFrameInfo{};
				denoiserFrame.frameIndex = frameCounter;
				denoiserFrame.resetHistory = resetHistory;
				denoiserFrame.cameraMoved = current.position != previous.position || current.forward != previous.forward;
				denoiserFrame.jitter = jitter;
				denoiserFrame.verticalFov = camera->GetFov();
				denoiserFrame.nearPlane = camera->GetNearPlane();
				denoiserFrame.farPlane = camera->GetFarPlane();
				denoiserFrame.view = glm::lookAt(current.position, current.position + current.forward, current.up);
				denoiserFrame.projection = glm::perspective(camera->GetFov(), static_cast<float>(extent.width) / static_cast<float>(extent.height), camera->GetNearPlane(), camera->GetFarPlane());
				denoiserFrame.aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
				denoiserFrame.deltaTime = deltaTime;

				previousCamera = current;
				resetHistory = false;
			}

			void VulkanRHI::RecordFrame(FrameResource& frameResource, uint32_t imageIndex) {
				auto& pipelineManager = Managers::PipelineManager::Get();
				auto& raytracingPipeline = pipelineManager.GetRayTracingPipeline(Managers::RayTracingPipelineCategory::MAIN_RAYTRACING_PIPELINE);
				VkPipelineLayout raytracingPipelineLayout = pipelineManager.GetRayTracingPipelineLayout(Managers::RayTracingPipelineCategory::MAIN_RAYTRACING_PIPELINE)->Get();

				const VkExtent2D extent = capabilities.currentExtent;
				const VkPipelineBindPoint rayTracing = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
				const VkAccessFlags readWrite = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

				// The G-buffer and all denoiser images stay in VK_IMAGE_LAYOUT_GENERAL, so the stages
				// of the frame only need to be ordered with memory barriers.
				CommandList& commandList = *frameResource.commandList;

				commandList.Begin()
					// 1. Path trace into the G-buffer. The previous frame may still be reading these images.
					.MemoryBarrier(
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
						readWrite, VK_ACCESS_SHADER_WRITE_BIT)
					.BindPipeline(raytracingPipeline->GetPipeline(), rayTracing)
					.BindDescriptorSets(raytracingPipelineLayout, 0,
						{ frameResource.perFrameDescriptorSet->Get(), Managers::TextureManager::Get().bindlessTextureDescriptorSet->Get() },
						rayTracing)
					.Raytrace(
						raytracingPipeline->GetRaygenRegion(),
						raytracingPipeline->GetMissRegion(),
						raytracingPipeline->GetHitRegion(),
						raytracingPipeline->GetCallableRegion(),
						extent.width, extent.height)

					// 2. Denoise
					.MemoryBarrier(
						VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_ACCESS_SHADER_WRITE_BIT, readWrite);

				denoisers[activeDenoiser]->Denoise(commandList, denoiserFrame);

				// 3. Tone map the denoised HDR image into the swapchain image
				commandList.MemoryBarrier(
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

				postProcessor->Record(commandList, swapchain->GetFramebuffer(imageIndex), extent);

				commandList.End();
			}

			VkResult VulkanRHI::Present(uint32_t imageIndex, VkSemaphore waitSemaphore) {
				VkSwapchainKHR swapchainHandle = swapchain->Get();

				VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
				presentInfo.waitSemaphoreCount = 1;
				presentInfo.pWaitSemaphores = &waitSemaphore;
				presentInfo.swapchainCount = 1;
				presentInfo.pSwapchains = &swapchainHandle;
				presentInfo.pImageIndices = &imageIndex;

				return vkQueuePresentKHR(presentQueue, &presentInfo);
			}

			void VulkanRHI::Resize(uint32_t /*width*/, uint32_t /*height*/) {
				// The surface is the source of truth for the size, the arguments are only a hint
				vkDeviceWaitIdle(device);
				vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

				const VkExtent2D extent = capabilities.currentExtent;
				if (extent.width == 0 || extent.height == 0) {
					return; // Minimized - Render() skips frames until the window is restored
				}

				// Destroy the old swapchain first: its framebuffers reference the depth texture
				renderFinishedSemaphores.clear();
				swapchain = nullptr;
				Managers::TextureManager::Get().Resize(extent);
				CreateSwapchain();

				CreateGBuffer();
				BindGBuffer();

				auto& camera = Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera();
				camera->SetAspectRatio(static_cast<float>(extent.width) / static_cast<float>(extent.height));
			}

			// ------------------------------------------------------------------------------------
			// Input
			// ------------------------------------------------------------------------------------

			void VulkanRHI::RegisterInput(Fox::Input::InputManager& input) {
				inputManager = &input;

				input.SetRelativeMode(true);

				mouseInputBind = input.OnMouseMoved.Connect([this](int dx, int dy) {
					if (inputManager->IsRelativeMode()) {
						Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera()->Rotate(
							static_cast<float>(dx) * mouseSensitivity, -static_cast<float>(dy) * mouseSensitivity);
					}
				});

				keyInputBind = input.OnKeyReleased.Connect([this](SDL_Scancode key) {
					using Managers::TextureManager;

					switch (key) {
					case SDL_SCANCODE_N:
						SelectDenoiser(activeDenoiser + 1);
						break;

					// Exposure, in thirds of a stop like a camera dial
					case SDL_SCANCODE_1:
					case SDL_SCANCODE_2:
						postProcessor->SetExposure(postProcessor->GetExposure() + (key == SDL_SCANCODE_2 ? 1.0f : -1.0f) / 3.0f);
						std::cout << "Exposure: " << postProcessor->GetExposure() << " EV\n";
						break;

					// Paths per pixel: quality against frame rate
					case SDL_SCANCODE_3:
					case SDL_SCANCODE_4:
						samplesPerPixel = static_cast<uint32_t>(std::clamp(static_cast<int>(samplesPerPixel) + (key == SDL_SCANCODE_4 ? 1 : -1), 1, 16));
						std::cout << "Samples per pixel: " << samplesPerPixel << "\n";
						break;

					case SDL_SCANCODE_9:
					case SDL_SCANCODE_0:
						TextureManager::ENVIRONMENT_MAP_INTENSITY = std::max(0.0f, TextureManager::ENVIRONMENT_MAP_INTENSITY + (key == SDL_SCANCODE_0 ? 0.1f : -0.1f));
						TextureManager::Get().UpdateEnvironment();
						std::cout << "Environment map intensity: " << TextureManager::ENVIRONMENT_MAP_INTENSITY << "\n";
						// The lighting changed: accumulated frames are stale
						ResetHistory();
						break;

					default:
						break;
					}
				});
			}

		}
	}
}
