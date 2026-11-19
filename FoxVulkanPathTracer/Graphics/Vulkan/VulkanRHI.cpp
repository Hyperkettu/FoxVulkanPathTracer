#include "FoxRenderer.h"
#include "FoxVulkanPathTracer/Core/TinyGLTF/json.hpp"

#include <fstream>
#include <thread>
#pragma comment(lib, "winmm.lib")

// The system timer's resolution (winmm), declared here: windows.h would bring its macros (min, max, MemoryBarrier)
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int period);

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
					VK_KHR_RAY_QUERY_EXTENSION_NAME, // Inline shadow rays in raygen.slang
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

				profiler = std::make_unique<GpuProfiler>(device, physicalDevice, graphicsQueueFamily, config.MAX_FRAMES_IN_FLIGHT);

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
				if (opacityMicromaps) {
					RayTracing::OpacityMicromapBuilder::RegisterExtensionFunctions(device);
				}

				raytracingScene = std::make_unique<Fox::Scene::RayTracing::MainRayTracingScene>(device, physicalDevice, graphicsQueue, graphicsQueueFamily);

				deviceContext.instance = instance;
				deviceContext.physicalDevice = physicalDevice;
				deviceContext.device = device;
				deviceContext.graphicsQueue = graphicsQueue;
				deviceContext.graphicsQueueFamily = graphicsQueueFamily;
				deviceContext.setupCommandPool = setupCommandPool;

				CreateGBuffer();
				denoisers = RayTracing::CreateDenoisers(deviceContext);

				// Object picking result, one entry (uint2) per frame in flight, read back by the CPU
				pickBuffer = std::make_unique<Buffer>(device, physicalDevice, sizeof(uint32_t) * 2 * config.MAX_FRAMES_IN_FLIGHT,
					VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				pickPending.assign(config.MAX_FRAMES_IN_FLIGHT, false);

				Managers::DescriptorManager::Get().Initialize(device, config);
				CreateFrameDescriptors();

				Managers::PipelineManager::Get().Initialize(device, physicalDevice, capabilities, shaderExecutionReordering);

				postProcessor = std::make_unique<PostProcessor>(device);
				overlay = std::make_unique<OverlayRenderer>(device, physicalDevice,
					Managers::RenderPassManager::Get().GetPass(Managers::RenderPass::DEFAULT)->Get(), config.MAX_FRAMES_IN_FLIGHT);
				BindGBuffer();

				InitializeUserInterface();
				textureVersion = raytracingScene->GetTextureLibrary().GetVersion();

				std::cout << "Vulkan renderer initialized (" << capabilities.currentExtent.width << "x" << capabilities.currentExtent.height << ").\n";
				return true;
			}

			void VulkanRHI::Destroy() {
				if (device != VK_NULL_HANDLE) {
					vkDeviceWaitIdle(device);

					mouseInputBind.Disconnect();
					keyInputBind.Disconnect();
					mouseButtonPressBind.Disconnect();
					mouseButtonReleaseBind.Disconnect();

					if (userInterface) {
						ImGui_ImplVulkan_Shutdown();
						ImGui_ImplSDL2_Shutdown();
						ImGui::DestroyContext();
						userInterface = false;
					}

					for (auto& frameResource : frameResources) {
						frameResource->Destroy(device);
					}
					frameResources.clear();

					postProcessor = nullptr;
					overlay = nullptr;
					pickBuffer = nullptr;
					profiler = nullptr;
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

						// For the Auto render preset: a laptop GPU, an integrated one, or one with little memory of its own
						gpuName = properties.deviceName;
						VkPhysicalDeviceMemoryProperties memory{};
						vkGetPhysicalDeviceMemoryProperties(candidate, &memory);
						gpuMemory = 0;
						for (uint32_t i = 0; i < memory.memoryHeapCount; i++) {
							if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
								gpuMemory = std::max<uint64_t>(gpuMemory, memory.memoryHeaps[i].size);
							}
						}
						modestGpu = !isDiscrete || gpuName.find("Laptop") != std::string::npos
							|| gpuMemory < 10ull * 1024 * 1024 * 1024;
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
				VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
				rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
				rayQueryFeatures.rayQuery = VK_TRUE; // Inline shadow rays

				VkPhysicalDeviceRayTracingPipelineFeaturesKHR raytracingFeatures{};
				raytracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
				raytracingFeatures.pNext = &rayQueryFeatures;
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
				vulkan13Features.synchronization2 = VK_TRUE; // Micromap build barriers

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

				// Optional: how much video memory is in use and available (the GPU Profiler window)
				if (std::any_of(available.begin(), available.end(), [](const VkExtensionProperties& e) {
					return std::strcmp(e.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0; })) {
					extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
					memoryBudget = true;
				}

				// Optional: shader execution reordering (NVIDIA RTX 40 series and newer). The path tracer then
				// regroups threads by what their bounce rays hit (raygen_ser variant of raygen.slang).
				VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV reorderFeatures{};
				reorderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_NV;
				{
					const bool hasExtension = std::any_of(available.begin(), available.end(), [](const VkExtensionProperties& e) {
						return std::strcmp(e.extensionName, VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME) == 0;
					});

					if (hasExtension) {
						VkPhysicalDeviceFeatures2 features2{};
						features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
						features2.pNext = &reorderFeatures;
						vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
					}

					shaderExecutionReordering = hasExtension && reorderFeatures.rayTracingInvocationReorder == VK_TRUE;
					if (shaderExecutionReordering) {
						extensions.push_back(VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME);
						reorderFeatures.pNext = vulkan12Features.pNext;
						vulkan12Features.pNext = &reorderFeatures;
					}
					std::cout << "Shader execution reordering: " << (shaderExecutionReordering ? "on" : "not supported") << "\n";
				}

				// Optional: opacity micromaps, so alpha tested geometry (leaves) is mostly resolved in hardware.
				// Without them the any-hit shader alpha tests every candidate hit, which is correct but slower.
				VkPhysicalDeviceOpacityMicromapFeaturesEXT micromapFeatures{};
				micromapFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT;
				{
					const bool hasExtension = std::any_of(available.begin(), available.end(), [](const VkExtensionProperties& e) {
						return std::strcmp(e.extensionName, VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME) == 0;
					});

					if (hasExtension) {
						VkPhysicalDeviceFeatures2 features2{};
						features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
						features2.pNext = &micromapFeatures;
						vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
					}

					opacityMicromaps = hasExtension && micromapFeatures.micromap == VK_TRUE;
					// For comparisons: FOX_NO_OPACITY_MICROMAPS=1 leaves all alpha testing to the any-hit shader
					if (SDL_getenv("FOX_NO_OPACITY_MICROMAPS")) opacityMicromaps = false;
					if (opacityMicromaps) {
						extensions.push_back(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME);
						micromapFeatures.micromapCaptureReplay = VK_FALSE;
						micromapFeatures.micromapHostCommands = VK_FALSE;
						micromapFeatures.pNext = vulkan12Features.pNext;
						vulkan12Features.pNext = &micromapFeatures;
					}
					Fox::Scene::RayTracing::RayTracingScene::EnableOpacityMicromaps(opacityMicromaps);
					std::cout << "Opacity micromaps: " << (opacityMicromaps ? "on" : "not supported") << "\n";
				}

				// Optional: cluster acceleration structures (NVIDIA RTX Mega Geometry), for the trees' continuous level of
				// detail (Graphics/Geometry/ClusterLod.h). Without them trees use one acceleration structure at full detail.
				VkPhysicalDeviceClusterAccelerationStructureFeaturesNV clusterFeatures{};
				clusterFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV;
				{
					const bool hasExtension = std::any_of(available.begin(), available.end(), [](const VkExtensionProperties& e) {
						return std::strcmp(e.extensionName, VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0;
					});
					if (hasExtension) {
						VkPhysicalDeviceFeatures2 features2{};
						features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
						features2.pNext = &clusterFeatures;
						vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
					}
					clusterLod = hasExtension && clusterFeatures.clusterAccelerationStructure == VK_TRUE;
					// For comparisons: FOX_NO_CLUSTER_LOD=1 traces trees at full detail, as before
					if (SDL_getenv("FOX_NO_CLUSTER_LOD")) clusterLod = false;
					if (clusterLod) {
						extensions.push_back(VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME);
						clusterFeatures.pNext = vulkan12Features.pNext;
						vulkan12Features.pNext = &clusterFeatures;
					}
					Fox::Scene::RayTracing::RayTracingScene::EnableClusterLod(clusterLod);
					std::cout << "Cluster level of detail: " << (clusterLod ? "on" : "not supported") << "\n";
				}

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
				// Vsync: FIFO, always available. Without it the frame rate is uncapped, which shows the real
				// cost of a frame (with vsync the GPU lowers its clocks to just fit the refresh interval).
				VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
				if (!vsync) {
					uint32_t count = 0;
					vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr);
					std::vector<VkPresentModeKHR> modes(count);
					vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, modes.data());

					for (VkPresentModeKHR preferred : { VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR }) {
						if (std::find(modes.begin(), modes.end(), preferred) != modes.end()) {
							presentMode = preferred;
							break;
						}
					}
				}

				swapchain = std::make_unique<Swapchain>(
					device,
					Managers::RenderPassManager::Get().GetPass(Managers::RenderPass::DEFAULT)->Get(),
					Managers::TextureManager::Get().GetDepthTexture(Managers::DepthTexture::DEFAULT_DEPTH_STENCIL)->GetView(),
					surface,
					surfaceFormat,
					capabilities,
					presentMode);

				renderFinishedSemaphores.clear();
				for (uint32_t i = 0; i < swapchain->GetImageCount(); i++) {
					renderFinishedSemaphores.push_back(std::make_unique<Semaphore>(device));
				}
			}

			void VulkanRHI::CreateGBuffer() {
				const VkExtent2D displayExtent = capabilities.currentExtent;

				// Until the user picks a mode (U), the render preset's
				if (!presetLoaded) {
					LoadRenderSettings();
				}
				if (!upscalingModeChosenByUser) {
					upscalingMode = SettingsOf(EffectivePreset()).upscaling;
				}

				gbuffer = std::make_unique<RayTracing::GBuffer>(deviceContext, RayTracing::RenderExtentFor(displayExtent, upscalingMode));
				ResetHistory();
			}

			// ------------------------------------------------------------------------------------------------------
			// Render presets
			// ------------------------------------------------------------------------------------------------------

			const char* VulkanRHI::PresetName(RenderPreset preset) {
				switch (preset) {
				case RenderPreset::Quality: return "Quality";
				case RenderPreset::Balanced: return "Balanced";
				case RenderPreset::Performance: return "Performance";
				default: return "Auto";
				}
			}

			VulkanRHI::RenderPreset VulkanRHI::EffectivePreset() const {
				return preset != RenderPreset::Auto ? preset : modestGpu ? RenderPreset::Balanced : RenderPreset::Quality;
			}

			VulkanRHI::PresetSettings VulkanRHI::SettingsOf(RenderPreset which) const {
				using Mode = RayTracing::UpscalingMode;
				switch (which) {
				case RenderPreset::Performance:
					// A quarter of the pixels, two bounces, 60 fps at most (a laptop stays cooler, so faster)
					return { Mode::Performance, 2, 60.0f };
				case RenderPreset::Balanced:
					return { Mode::Balanced, 3, 60.0f };
				default: {
					// Full paths; resolution from the display size. At 4K, Quality (1440p) keeps a 60 fps frame well
					// within budget (~4.5 ms on an RTX 5090; Native takes ~8.6 ms).
					const bool large = capabilities.currentExtent.height >= 2000;
					return { large ? Mode::Quality : Mode::Native, 4, 0.0f };
				}
				}
			}

			void VulkanRHI::LoadRenderSettings() {
				presetLoaded = true;
				try {
					std::ifstream in("Settings/Render.json");
					if (in) {
						const nlohmann::json j = nlohmann::json::parse(in, nullptr, true, true);
						const std::string name = j.value("preset", std::string("Auto"));
						for (RenderPreset p : { RenderPreset::Auto, RenderPreset::Quality, RenderPreset::Balanced, RenderPreset::Performance }) {
							if (name == PresetName(p)) preset = p;
						}
					}
				}
				catch (const std::exception& e) {
					std::cout << "Settings/Render.json: " << e.what() << "\n";
				}
				const PresetSettings settings = SettingsOf(EffectivePreset());
				maxBounces = settings.bounces;
				frameCap = settings.frameCap;
				std::cout << "Render preset: " << PresetName(preset);
				if (preset == RenderPreset::Auto) {
					std::cout << " (" << PresetName(EffectivePreset()) << ": " << gpuName << ", " << (gpuMemory >> 20) << " MiB"
						<< (modestGpu ? ", a laptop or mid-range GPU" : "") << ")";
				}
				std::cout << " - " << settings.bounces << " bounces" << (settings.frameCap > 0.0f ? ", 60 fps at most" : "") << "\n";
			}

			void VulkanRHI::SaveRenderSettings() const {
				try {
					std::filesystem::create_directories("Settings");
					std::ofstream out("Settings/Render.json");
					out << nlohmann::json{ { "preset", PresetName(preset) } }.dump(2) << "\n";
				}
				catch (const std::exception& e) {
					std::cout << "Settings/Render.json: " << e.what() << "\n";
				}
			}

			void VulkanRHI::SetRenderPreset(RenderPreset newPreset) {
				preset = newPreset;
				SaveRenderSettings();
				const PresetSettings settings = SettingsOf(EffectivePreset());
				maxBounces = settings.bounces;
				frameCap = settings.frameCap;
				std::cout << "Render preset: " << PresetName(preset) << (preset == RenderPreset::Auto ? std::string(" (") + PresetName(EffectivePreset()) + ")" : "")
					<< " - " << settings.bounces << " bounces" << (settings.frameCap > 0.0f ? ", 60 fps at most" : "") << "\n";
				// Its resolution, instead of one picked by hand
				SetUpscalingMode(settings.upscaling);
				upscalingModeChosenByUser = false;
				UpdateWindowTitle(0.0f);
			}

			void VulkanRHI::WaitForFrameCap() {
				const auto now = std::chrono::steady_clock::now();
				if (frameCap > 0.0f && lastFrameStart.time_since_epoch().count() != 0) {
					const auto target = lastFrameStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / frameCap));
					// Sleep most of the way (the timer is coarse), then spin the last bit
					static const bool fineTimer = timeBeginPeriod(1) == 0; // 1 ms sleeps
					(void)fineTimer;
					if (target - now > std::chrono::milliseconds(2)) {
						std::this_thread::sleep_for(target - now - std::chrono::milliseconds(2));
					}
					while (std::chrono::steady_clock::now() < target) {
						std::this_thread::yield();
					}
					lastFrameStart = std::max(target, now);
					return;
				}
				lastFrameStart = now;
			}

			void VulkanRHI::SetUpscalingMode(RayTracing::UpscalingMode mode) {
				// The G-buffer and the denoisers' images are about to be replaced
				vkDeviceWaitIdle(device);

				upscalingMode = mode;
				upscalingModeChosenByUser = true;

				CreateGBuffer();
				BindGBuffer();

				std::cout << "Upscaling: " << RayTracing::UpscalingModeName(upscalingMode) << " (" << gbuffer->extent.width << "x" << gbuffer->extent.height
					<< " -> " << capabilities.currentExtent.width << "x" << capabilities.currentExtent.height << ")\n";
			}

			void VulkanRHI::WriteSceneDescriptors(FrameResource& frameResource) {
				auto& textureManager = Managers::TextureManager::Get();
				VkAccelerationStructureKHR tlas = raytracingScene->GetTLAS();

				auto& set = frameResource.perFrameDescriptorSet;
				set->ClearWrites();
				set->Reserve(16);
				set->SetAccelerationStructure(0, tlas);
				set->SetStorageImage(1, gbuffer->radiance->GetView());
				set->SetStorageImage(8, gbuffer->albedo->GetView());
				set->SetStorageImage(9, gbuffer->normalDepth->GetView());
				set->SetStorageImage(10, gbuffer->motion->GetView());
				set->SetStorageImage(11, gbuffer->specular->GetView());
				set->SetStorageBuffer(12, textureManager.environmentSamplingTable->GetBufferUnique());
				set->SetConstantBuffer(2, frameResource.camUBO);
				set->SetStorageBuffer(3, raytracingScene->vertexSSBO->GetBufferUnique());
				set->SetStorageBuffer(4, raytracingScene->indexSSBO->GetBufferUnique());
				set->SetStorageBuffer(5, raytracingScene->submeshSSBO->GetBufferUnique());
				set->SetStorageBuffer(6, raytracingScene->materialsSSBO->GetBufferUnique());
				set->SetStorageBuffer(7, raytracingScene->lightsSSBO->GetBufferUnique());
				set->SetStorageBuffer(13, raytracingScene->instanceMotionSSBO->GetBufferUnique());
				set->SetStorageBuffer(14, pickBuffer);
				set->SetStorageBuffer(15, raytracingScene->previousPositionsSSBO->GetBufferUnique());
				set->Update();
			}

			void VulkanRHI::CreateFrameDescriptors() {
				auto& descriptorManager = Managers::DescriptorManager::Get();
				auto& textureManager = Managers::TextureManager::Get();

				// Set 0: scene data, one copy per frame in flight because the camera buffer changes every frame.
				VkAccelerationStructureKHR tlas = raytracingScene->GetTLAS();

				for (auto& frameResource : frameResources) {
					frameResource->perFrameDescriptorSet = std::make_unique<DescriptorSet>(
						descriptorManager.GetDescriptorSet(Managers::Descriptor::RAY_TRACING)->AllocateSet());

					WriteSceneDescriptors(*frameResource);
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

				// A denoiser that cannot work at this size (DLSS: its feature is made here, and an old driver refuses it)
				// leaves the list; the others carry on, so the renderer still starts
				const RayTracing::OutputSettings output{ capabilities.currentExtent, upscalingMode };
				for (size_t i = 0; i < denoisers.size();) {
					try {
						denoisers[i]->Resize(*gbuffer, output);
						i++;
					}
					catch (const std::exception& e) {
						std::cout << denoisers[i]->GetName() << " is not available: " << e.what() << "\n";
						if (denoisers.size() == 1) {
							throw; // Nothing left to show the image with
						}
						denoisers.erase(denoisers.begin() + static_cast<std::ptrdiff_t>(i));
						if (activeDenoiser > i || activeDenoiser >= denoisers.size()) {
							activeDenoiser = activeDenoiser > 0 ? activeDenoiser - 1 : 0;
						}
					}
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
				title << "Fox Renderer - " << denoisers[activeDenoiser]->GetName() << " - " << RayTracing::UpscalingModeName(upscalingMode)
					<< " " << gbuffer->extent.width << "x" << gbuffer->extent.height << " - " << samplesPerPixel << " spp";
				auto& cameraManager = raytracingScene->GetCameraManager();
				if (cameraManager.IsPlaying() && cameraManager.ActivePath()) {
					title << std::fixed << std::setprecision(1) << " - PATH " << cameraManager.ActivePath()->name << " "
						<< cameraManager.GetTime() << " / " << cameraManager.ActivePath()->Duration() << " s";
					title.unsetf(std::ios::fixed);
				}
				if (editMode) {
					const auto selected = gizmo.GetSelected();
					const auto& graph = raytracingScene->GetSceneGraph();
					title << " - EDIT " << Fox::Editor::TransformGizmo::ModeName(gizmo.GetMode()) << ": "
						<< (graph.IsValid(selected) ? graph.GetName(selected) : std::string("click an object"));
				}
				if (titleTimer > 0.0f) {
					title << " - " << static_cast<int>(std::round(titleFrames / titleTimer)) << " fps" << (vsync ? "" : " (vsync off)");
				}
				if (raytracingScene->HasClusterMeshes()) {
					// Trees' level of detail: distinct clusters built this frame, and clusters traced over all instances
					const auto lod = raytracingScene->GetClusterLodStats();
					title << " - trees " << lod.instances << ": " << lod.clustersBuilt << " clusters built, " << lod.clustersSelected << " traced";
				}

				// The frame rate is capped by vsync; the GPU time shows the actual cost and headroom
				if (profiler && profiler->GetFrameMilliseconds() > 0.0f) {
					title << std::fixed << std::setprecision(1) << " - GPU " << profiler->GetFrameMilliseconds() << " ms (" << profiler->GetSectionSummary() << ")";

					// Also on the console every few seconds, for logs and benchmarks
					consoleTimer += titleTimer;
					if (consoleTimer >= 2.0f) {
						consoleTimer = 0.0f;
						std::cout << "[GPU] " << profiler->GetFrameMilliseconds() << " ms: " << profiler->GetSectionSummary()
							<< " (" << gbuffer->extent.width << "x" << gbuffer->extent.height << " -> "
							<< capabilities.currentExtent.width << "x" << capabilities.currentExtent.height << ", "
							<< denoisers[activeDenoiser]->GetName() << ", " << samplesPerPixel << " spp)\n";
						std::cout.unsetf(std::ios::floatfield);
					}
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

				// The preset's frame cap: less heat on a laptop, so it keeps its clocks up
				WaitForFrameCap();

				// Menu commands of the previous frame that replace rendering resources (as their keys do)
				RunDeferredCommands();

				HandleCameraInput(deltaTime);
				UpdateWindowTitle(deltaTime);
				cpuHistory[cpuHistoryNext] = deltaTime * 1000.0f;
				cpuHistoryNext = (cpuHistoryNext + 1) % cpuHistory.size();

				FrameResource& frameResource = *frameResources[currentFrame];

				// Block until the GPU has finished the work that last used this frame slot
				frameResource.renderFence->Wait();
				profiler->ReadResults(currentFrame);
				raytracingScene->ReadClusterLodStats(currentFrame);
				ReadPickResult(currentFrame);

				// Editor windows, and what they changed in the scene
				BuildUserInterface();
				ApplySceneChanges();

				// Terrain brush strokes, and what they changed on its way to the GPU
				const bool ctrl = inputManager && (inputManager->IsKeyDown(SDL_SCANCODE_LCTRL) || inputManager->IsKeyDown(SDL_SCANCODE_RCTRL));
				terrainEditor.Update(*raytracingScene, GizmoView(), MouseInDisplayPixels(), deltaTime, ctrl);
				// Mesh paint: Ctrl erases
				meshPainter.Update(*raytracingScene, GizmoView(), MouseInDisplayPixels(), ctrl);

				// Animation (wind in the trees); recorded into this frame by RecordFrameUpdate
				UpdateScriptInput();
				raytracingScene->Update(deltaTime);
				FollowGameCamera();
				{
					auto& listener = Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera();
					raytracingScene->SetListener(listener->GetPosition(), listener->GetForward(), listener->GetUp());
				}
				// Entities moved, shown or hidden from the editor windows (the gizmos say so while dragging)
				sceneChanged = raytracingScene->TakeObjectsChanged() || sceneChanged;

				frameResource.renderFence->Reset();
				frameResource.commandPool->Reset();

				// The frame is two submits. Path tracing and denoising, nearly all of the work, need no
				// swapchain image and start right away. Only tone mapping waits for the image: with vsync an
				// image is released at a display refresh, and waiting for it before tracing would leave the
				// trace a single refresh interval to finish in (any slower frame then misses a refresh).
				UpdateCameraUniforms(frameResource, deltaTime);
				RecordTrace(frameResource);
				frameResource.commandList->Submit(graphicsQueue);

				uint32_t imageIndex = 0;
				VkResult acquireResult = swapchain->AcquireNextImage(frameResource.imageAvailableSemaphore->Get(), imageIndex);
				if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
					// The fence was reset: signal it with an empty submit so the next Wait() returns
					frameResource.presentCommandList->Begin().End();
					frameResource.presentCommandList->Submit(graphicsQueue, VK_NULL_HANDLE, VK_NULL_HANDLE, frameResource.renderFence->Get());
					Resize(0, 0);
					return;
				}

				RecordToneMapping(frameResource, imageIndex);

				const VkSemaphore renderFinished = renderFinishedSemaphores[imageIndex]->Get();

				frameResource.presentCommandList->Submit(
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
				// Not while typing in a text field: W A S D R F are letters there
				const bool typing = userInterface && ImGui::GetIO().WantTextInput;
				auto key = [this, typing](SDL_Scancode code) { return !typing && inputManager->IsKeyDown(code) ? 1.0f : 0.0f; };
				const glm::vec3 move(
					key(SDL_SCANCODE_D) - key(SDL_SCANCODE_A),
					key(SDL_SCANCODE_R) - key(SDL_SCANCODE_F),
					key(SDL_SCANCODE_W) - key(SDL_SCANCODE_S));

				if (LooksThroughGameCamera()) {
					return; // The game's camera places it (FollowGameCamera)
				}

				// Free flight, or along the camera path while it plays
				auto& cameraManager = raytracingScene->GetCameraManager();
				const bool wasPlaying = cameraManager.IsPlaying();
				cameraManager.Update(deltaTime, move, *camera);
				if (wasPlaying && !cameraManager.IsPlaying()) {
					std::cout << "Camera path: finished\n";
					UpdateWindowTitle(0.0f);
				}
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

				// The path tracer renders at the G-buffer's resolution, which may be below the display's
				const VkExtent2D extent = gbuffer->extent;
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

				// Object picking: the pixel under the mouse, in render resolution
				cam.pickPixel = glm::ivec2(-1, -1);
				cam.pickSlot = currentFrame;
				shaderTime += deltaTime;
				cam.time = raytracingScene->GetAnimationTime();
				cam.maxBounces = maxBounces;
				raytracingScene->FillVolumeConstants(cam);
				if (pickRequested) {
					const glm::vec2 display(capabilities.currentExtent.width, capabilities.currentExtent.height);
					cam.pickPixel = glm::ivec2(pickMouse / display * glm::vec2(extent.width, extent.height));
					pickPending[currentFrame] = true;
					pickRequested = false;
				}

				frameResource.camUBO->Update(cam);

				// What the denoiser needs to know about this frame
				denoiserFrame = RayTracing::DenoiserFrameInfo{};
				denoiserFrame.frameIndex = frameCounter;
				denoiserFrame.resetHistory = resetHistory;
				// Moved objects also invalidate what accumulated (the other denoisers follow them with motion vectors)
				denoiserFrame.cameraMoved = current.position != previous.position || current.forward != previous.forward || sceneChanged;
				sceneChanged = false;
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

			void VulkanRHI::RecordTrace(FrameResource& frameResource) {
				auto& pipelineManager = Managers::PipelineManager::Get();
				auto& raytracingPipeline = pipelineManager.GetRayTracingPipeline(Managers::RayTracingPipelineCategory::MAIN_RAYTRACING_PIPELINE);
				VkPipelineLayout raytracingPipelineLayout = pipelineManager.GetRayTracingPipelineLayout(Managers::RayTracingPipelineCategory::MAIN_RAYTRACING_PIPELINE)->Get();

				const VkExtent2D renderExtent = gbuffer->extent;
				const VkPipelineBindPoint rayTracing = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
				const VkAccessFlags readWrite = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

				// The G-buffer and all denoiser images stay in VK_IMAGE_LAYOUT_GENERAL, so the stages
				// of the frame only need to be ordered with memory barriers.
				CommandList& commandList = *frameResource.commandList;

				commandList.Begin();

				// Trees' level of detail is chosen for this view: errors in pixels of the displayed image
				{
					auto& camera = Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera();
					const float pixelScale = static_cast<float>(capabilities.currentExtent.height) / (2.0f * std::tan(camera->GetFov() * 0.5f));
					raytracingScene->SetClusterLodView(camera->GetPosition(), pixelScale);
				}

				// Objects moved since the last frame: acceleration structure, lights, motion (timed as "scene": skinning,
				// the trees' cluster structures and the top level build)
				profiler->BeginFrame(commandList, currentFrame);
				raytracingScene->RecordFrameUpdate(commandList.Get(), currentFrame, [&](const char* name) { profiler->Mark(commandList, name); });
				profiler->Mark(commandList, "scene");

				commandList
					// 1. Path trace into the G-buffer. The previous frame may still be reading these images.
					.MemoryBarrier(
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
						readWrite, VK_ACCESS_SHADER_WRITE_BIT)
					.BindPipeline(raytracingPipeline->GetPipeline(), rayTracing)
					.BindDescriptorSets(raytracingPipelineLayout, 0,
						{ frameResource.perFrameDescriptorSet->Get(), Managers::TextureManager::Get().bindlessTextureDescriptorSet->Get() },
						rayTracing);

				commandList.Raytrace(
					raytracingPipeline->GetRaygenRegion(),
					raytracingPipeline->GetMissRegion(),
					raytracingPipeline->GetHitRegion(),
					raytracingPipeline->GetCallableRegion(),
					renderExtent.width, renderExtent.height);

				profiler->Mark(commandList, "trace");

				// 2. Denoise
				commandList.MemoryBarrier(
					VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_ACCESS_SHADER_WRITE_BIT, readWrite);

				denoisers[activeDenoiser]->Denoise(commandList, denoiserFrame);

				profiler->Mark(commandList, "denoise");

				commandList.End();
			}

			void VulkanRHI::RecordToneMapping(FrameResource& frameResource, uint32_t imageIndex) {
				const VkExtent2D displayExtent = capabilities.currentExtent;
				CommandList& commandList = *frameResource.presentCommandList;

				// The time between the two submits: waiting for the display to release the image
				commandList.Begin();
				profiler->Mark(commandList, "wait", true);

				// 3. Tone map the denoised HDR image into the swapchain image
				commandList.MemoryBarrier(
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

				// Also upscales the denoisers that output at render resolution (bilinear filtering)
				// Gizmos of the selected object, at display resolution on top of the image
				overlayVertices.clear();
				if (editMode) {
					static_assert(sizeof(OverlayRenderer::Vertex) == sizeof(Fox::Editor::TransformGizmo::OverlayVertex), "Overlay vertex layouts differ");
					std::vector<Fox::Editor::TransformGizmo::OverlayVertex> gizmoVertices;
					gizmo.BuildOverlay(GizmoView(), raytracingScene->GetSceneGraph(), gizmoVertices);
					for (const auto& vertex : gizmoVertices) {
						overlayVertices.push_back({ vertex.position, vertex.color });
					}
				}

				postProcessor->SetGrade(raytracingScene->GetEnvironment().image);
				postProcessor->Record(commandList, swapchain->GetFramebuffer(imageIndex), displayExtent,
					[this, displayExtent](CommandList& pass) {
						overlay->Record(pass, overlayVertices, currentFrame, displayExtent);
						if (userInterface) {
							ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), pass.Get());
						}
					});

				profiler->Mark(commandList, "tonemap");

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

			void VulkanRHI::InitializeUserInterface() {
				IMGUI_CHECKVERSION();
				ImGui::CreateContext();
				ImGuiIO& io = ImGui::GetIO();
				io.IniFilename = nullptr; // No imgui.ini next to the executable
				// No keyboard navigation: the keyboard stays with the camera and shortcuts (WASD, E, Z/X/C) unless
				// a text field is being typed in

				ImGui_ImplSDL2_InitForVulkan(static_cast<SDL_Window*>(config.windowHandle));

				ImGui_ImplVulkan_InitInfo info{};
				info.ApiVersion = VK_API_VERSION_1_4;
				info.Instance = instance;
				info.PhysicalDevice = physicalDevice;
				info.Device = device;
				info.QueueFamily = graphicsQueueFamily;
				info.Queue = graphicsQueue;
				info.DescriptorPoolSize = 16; // The backend makes its own pool (the font texture)
				info.MinImageCount = 2;
				info.ImageCount = std::max(swapchain->GetImageCount(), 2u);
				info.PipelineInfoMain.RenderPass = Managers::RenderPassManager::Get().GetPass(Managers::RenderPass::DEFAULT)->Get();
				info.PipelineInfoMain.Subpass = 0;
				info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
				if (!ImGui_ImplVulkan_Init(&info)) {
					throw std::runtime_error("Failed to initialize the user interface");
				}
				userInterface = true;
			}

			void VulkanRHI::BuildUserInterface() {
				if (!userInterface) {
					return;
				}

				// Bigger on bigger windows: 1 at 1080 lines
				const float scale = std::clamp(static_cast<float>(capabilities.currentExtent.height) / 1080.0f, 1.0f, 3.0f);
				if (scale != uiScale) {
					uiScale = scale;
					ImGuiStyle style;
					ImGui::StyleColorsDark(&style);
					style.WindowRounding = 4.0f;
					style.FrameRounding = 3.0f;
					style.ScaleAllSizes(scale);
					style.FontScaleDpi = scale;
					ImGui::GetStyle() = style;
				}

				ImGui_ImplVulkan_NewFrame();
				ImGui_ImplSDL2_NewFrame();
				ImGui::NewFrame();

				// Ctrl+Space: the content browser; F1: find a command. Both bring up edit mode first.
				const bool contentKey = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Space);
				const bool findKey = ImGui::IsKeyPressed(ImGuiKey_F1, false);
				if ((contentKey || findKey) && !editMode) {
					SetEditMode(true);
				}
				if (contentKey) {
					shell.Toggle(Fox::Editor::EditorShell::Window::ContentBrowser);
				}
				if (findKey) {
					shell.FocusFind();
				}

				if (editMode) {
					BuildEditor();
				}

				ImGui::Render();
			}

			void VulkanRHI::ApplySceneChanges() {
				// While a slider is dragged the change waits for its release, unless live update is on
				const bool dragging = userInterface && ImGui::IsAnyItemActive();

				// Objects added or removed, a scene file loaded, the environment changed: everything is rebuilt,
				// and every scene resource the descriptors point at is new
				if (raytracingScene->HasStructureChanged() && !dragging) {
					vkDeviceWaitIdle(device);
					const auto start = std::chrono::steady_clock::now();
					std::string error;
					const bool succeeded = raytracingScene->RebuildScene(error);
					const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();

					for (auto& frameResource : frameResources) {
						WriteSceneDescriptors(*frameResource);
					}
					RefreshTextureDescriptors(); // Models bring their own textures
					textureVersion = raytracingScene->GetTextureLibrary().GetVersion();

					sceneEditor.Rebuilt(succeeded, error, seconds); // The selection is an entity: it stays
					std::cout << "Scene: rebuilt in " << seconds << " s" << (succeeded ? "" : " (" + error + ")") << "\n";
					ResetHistory();
				}

				// A loaded scene file's view
				Fox::Scene::RayTracing::MainRayTracingScene::CameraPose pose;
				if (raytracingScene->TakePendingCamera(pose)) {
					auto& camera = Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera();
					camera->SetPosition(pose.position);
					camera->SetCameraTarget(pose.target);
					ResetHistory();
				}

				if (raytracingScene->HaveTreesChanged() && (treeEditor.LiveUpdate() || !dragging)) {
					vkDeviceWaitIdle(device);
					const auto start = std::chrono::steady_clock::now();
					std::string error;
					const bool succeeded = raytracingScene->ApplyTreeChanges(error);
					const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
					treeEditor.Applied(*raytracingScene, gizmo, succeeded, error, seconds);
					if (!succeeded) {
						std::cout << "Trees: " << error << "\n";
					}
					else {
						std::cout << "Trees: " << raytracingScene->GetTreeEntities().size() << " rebuilt in " << seconds * 1000.0f << " ms\n";
					}
					sceneChanged = true;
				}

				// Textures loaded for new materials join the bindless array
				if (raytracingScene->GetTextureLibrary().GetVersion() != textureVersion) {
					vkDeviceWaitIdle(device);
					RefreshTextureDescriptors();
					textureVersion = raytracingScene->GetTextureLibrary().GetVersion();
				}
			}

			void VulkanRHI::RefreshTextureDescriptors() {
				auto& textureManager = Managers::TextureManager::Get();
				auto& set = textureManager.bindlessTextureDescriptorSet;
				const auto& textures = textureManager.GetBindlessTextureArray();

				set->ClearWrites();
				set->Reserve(static_cast<uint32_t>(textures.size()) + 1);
				uint32_t index = 0;
				for (const auto& texture : textures) {
					set->SetBindlessTexture(1, index++, texture.get());
				}
				set->SetConstantBuffer(0, textureManager.environmentUBO);
				set->Update();
			}

			bool VulkanRHI::HandleEvent(const SDL_Event& event) {
				if (!userInterface) {
					return false;
				}
				ImGui_ImplSDL2_ProcessEvent(&event);
				if (!editMode) {
					return false;
				}

				// Clicks, wheel and typing over the editor windows are theirs. Releases always go through, so
				// nothing stays pressed.
				const ImGuiIO& io = ImGui::GetIO();
				switch (event.type) {
				case SDL_MOUSEBUTTONDOWN:
				case SDL_MOUSEWHEEL:
					return io.WantCaptureMouse;
				case SDL_KEYDOWN:
				case SDL_TEXTINPUT:
					return io.WantCaptureKeyboard;
				default:
					return false;
				}
			}

			void VulkanRHI::SetEditMode(bool enabled) {
				editMode = enabled;
				gizmo.MouseUp();
				terrainEditor.MouseUp();

				// Edit mode frees the mouse pointer for clicking; otherwise the mouse looks around
				if (inputManager) {
					inputManager->SetRelativeMode(!editMode);
				}

				std::cout << "Edit mode: " << (editMode ? "on (click an object, Z move, X rotate, C scale, Ctrl snaps, right mouse looks around)" : "off") << "\n";
				UpdateWindowTitle(0.0f);
			}

			glm::vec2 VulkanRHI::MouseInDisplayPixels() const {
				// SDL reports window coordinates, which can differ from the drawable's pixels (display scaling)
				int windowWidth = 0, windowHeight = 0;
				SDL_GetWindowSize(static_cast<SDL_Window*>(config.windowHandle), &windowWidth, &windowHeight);

				const glm::vec2 window(std::max(windowWidth, 1), std::max(windowHeight, 1));
				const glm::vec2 display(capabilities.currentExtent.width, capabilities.currentExtent.height);
				const glm::vec2 mouse(inputManager ? inputManager->GetMouseX() : 0, inputManager ? inputManager->GetMouseY() : 0);
				return (mouse + 0.5f) * display / window;
			}

			Fox::Editor::TransformGizmo::View VulkanRHI::GizmoView() const {
				auto& camera = Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera();

				Fox::Editor::TransformGizmo::View view;
				view.position = camera->GetPosition();
				view.forward = camera->GetForward();
				view.right = camera->GetRight();
				view.up = camera->GetUp();
				view.tanHalfFov = std::tan(camera->GetFov() * 0.5f);
				view.size = glm::vec2(capabilities.currentExtent.width, capabilities.currentExtent.height);
				return view;
			}

			void VulkanRHI::ReadPickResult(uint32_t frameSlot) {
				if (frameSlot >= pickPending.size() || !pickPending[frameSlot]) {
					return;
				}
				pickPending[frameSlot] = false;

				// The frame that asked has finished (its fence was waited on)
				uint32_t result[2] = {};
				std::memcpy(result, static_cast<const uint8_t*>(pickBuffer->Map()) + sizeof(result) * frameSlot, sizeof(result));
				pickBuffer->Unmap();

				// Instance index + 1, 0 for the sky. What is not an entity (the ground, the terrain) deselects.
				const int object = result[0] > 0 ? raytracingScene->GetObjectOfInstance(result[0] - 1) : -1;
				const auto entity = raytracingScene->GetEntityOfObject(object);
				gizmo.Select(entity);

				if (entity) {
					std::cout << "Selected: " << raytracingScene->GetSceneGraph().GetName(entity) << "\n";
				}
				UpdateWindowTitle(0.0f);
			}

			// ------------------------------------------------------------------------------------------------------
			// Play mode
			// ------------------------------------------------------------------------------------------------------

			void VulkanRHI::TogglePlay() {
				auto& camera = Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera();
				if (raytracingScene->IsPlaying()) {
					raytracingScene->StopPlay();
					// Back where the editor was looking from
					camera->SetPosition(editorView.position);
					camera->SetCameraTarget(editorView.position + editorView.forward);
					camera->SetFov(editorView.fov);
				}
				else {
					editorView.position = camera->GetPosition();
					editorView.forward = camera->GetForward();
					editorView.fov = camera->GetFov();
					std::string error;
					if (!raytracingScene->StartPlay(error)) {
						std::cout << "Play: " << error << "\n";
					}
				}
				cameraEjected = false;
				ResetHistory();
				UpdateWindowTitle(0.0f);
			}

			bool VulkanRHI::LooksThroughGameCamera() const {
				return raytracingScene && raytracingScene->IsPlaying() && !cameraEjected && raytracingScene->FindGameCamera();
			}

			void VulkanRHI::FollowGameCamera() {
				if (!LooksThroughGameCamera()) {
					return;
				}
				const auto entity = raytracingScene->FindGameCamera();
				const auto& graph = raytracingScene->GetSceneGraph();
				const glm::mat4 world = graph.GetWorldMatrix(entity);
				const glm::vec3 position(world[3]);
				glm::vec3 forward = -glm::vec3(world[2]);
				forward = glm::dot(forward, forward) > 1e-12f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
				auto& camera = Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera();
				camera->SetPosition(position);
				camera->SetCameraTarget(position + forward);
				const auto* component = graph.GetRegistry().TryGet<Fox::Scene::CameraComponent>(entity);
				camera->SetFov(glm::radians(glm::clamp(component ? component->fov : 60.0f, 5.0f, 170.0f)));
			}

			void VulkanRHI::RegisterInput(Fox::Input::InputManager& input) {
				inputManager = &input;

				input.SetRelativeMode(true);

				mouseInputBind = input.OnMouseMoved.Connect([this](int dx, int dy) {
					// Looking around: always outside edit mode, with the right mouse button held inside it
					// Not while the camera flies its path
					scriptMouseDelta += glm::vec2(static_cast<float>(dx), static_cast<float>(dy));
					const bool look = (inputManager->IsRelativeMode() || (editMode && inputManager->IsMouseDown(SDL_BUTTON_RIGHT)))
						&& !raytracingScene->GetCameraManager().IsPlaying() && !LooksThroughGameCamera();
					if (look) {
						Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera()->Rotate(
							static_cast<float>(dx) * mouseSensitivity, -static_cast<float>(dy) * mouseSensitivity);
					}

					if (editMode) {
						const bool snap = inputManager->IsKeyDown(SDL_SCANCODE_LCTRL) || inputManager->IsKeyDown(SDL_SCANCODE_RCTRL);
						gizmo.MouseMove(MouseInDisplayPixels(), GizmoView(), raytracingScene->GetSceneGraph(), snap);
						sceneChanged = sceneChanged || gizmo.IsDragging();
					}
				});

				mouseButtonPressBind = input.OnMouseButtonPressed.Connect([this](Uint8 button) {
					if (!editMode || button != SDL_BUTTON_LEFT) {
						return;
					}

					// Grab a handle of the gizmo, or select what is under the mouse (answered a frame or two later)
					const glm::vec2 mouse = MouseInDisplayPixels();
					if (meshPainter.MouseDown(*raytracingScene, GizmoView(), mouse)) {
						return; // Painting meshes
					}
					if (terrainEditor.MouseDown(*raytracingScene, GizmoView(), mouse)) {
						return; // Sculpting or painting
					}
					// Shift + click on the ground sends the selected agent there
					const bool shift = inputManager->IsKeyDown(SDL_SCANCODE_LSHIFT) || inputManager->IsKeyDown(SDL_SCANCODE_RSHIFT);
					const auto selected = gizmo.GetSelected();
					auto& graph = raytracingScene->GetSceneGraph();
					if (shift && graph.IsValid(selected) && graph.GetRegistry().Has<Fox::Scene::NavAgentComponent>(selected)) {
						glm::vec3 origin, direction, hit;
						Fox::Editor::TransformGizmo::MouseRay(GizmoView(), mouse, origin, direction);
						if (raytracingScene->RaycastGround(origin, direction, hit)) {
							raytracingScene->SetNavTarget(selected, hit);
							std::cout << "Navigation: " << graph.GetName(selected) << " to " << hit.x << ", " << hit.y << ", " << hit.z << "\n";
							return;
						}
					}
					if (!gizmo.MouseDown(mouse, GizmoView(), raytracingScene->GetSceneGraph())) {
						// Not a gizmo handle: a click on water (with splashing on) splashes it; anywhere else selects
						glm::vec3 origin, direction;
						Fox::Editor::TransformGizmo::MouseRay(GizmoView(), mouse, origin, direction);
						if (raytracingScene->SplashWater(origin, direction)) {
							return;
						}
						pickRequested = true;
						pickMouse = mouse;
					}
				});

				mouseButtonReleaseBind = input.OnMouseButtonReleased.Connect([this](Uint8 button) {
					if (button == SDL_BUTTON_LEFT) {
						gizmo.MouseUp();
						terrainEditor.MouseUp();
						meshPainter.MouseUp(*raytracingScene);
					}
				});

				keyInputBind = input.OnKeyReleased.Connect([this](SDL_Scancode key) {
					// Typing in a text field (a name, Find, Save As) is not a shortcut
					if (userInterface && ImGui::GetIO().WantTextInput) {
						return;
					}
					RunKeyCommand(key);
				});
			}

			void VulkanRHI::UpdateScriptInput() {
				// What scripts see this frame (Fox.Input): the keyboard unless a text field has it, the mouse buttons
				// unless the editor's windows have the pointer
				auto& input = raytracingScene->GetScriptInput();
				std::memcpy(input.previousKeys, input.keys, sizeof(input.keys));
				std::memcpy(input.previousButtons, input.buttons, sizeof(input.buttons));
				const bool typing = userInterface && editMode && ImGui::GetIO().WantTextInput;
				const bool overUi = userInterface && editMode && ImGui::GetIO().WantCaptureMouse;
				for (int code = 0; code < 512; code++) {
					input.keys[code] = !typing && inputManager && inputManager->IsKeyDown(static_cast<SDL_Scancode>(code)) ? 1 : 0;
				}
				const Uint8 buttons[3] = { SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT };
				for (int i = 0; i < 3; i++) {
					input.buttons[i] = !overUi && inputManager && inputManager->IsMouseDown(buttons[i]) ? 1 : 0;
				}
				input.mouseDelta = scriptMouseDelta;
				input.mousePosition = MouseInDisplayPixels();
				scriptMouseDelta = glm::vec2(0.0f);
			}

			void VulkanRHI::RunKeyCommand(SDL_Scancode key) {
				using Managers::TextureManager;
				using Mode = Fox::Editor::TransformGizmo::Mode;

				// A key pressed for a binding (Input Actions window) is not a command
				if (CapturingInput()) {
					return;
				}

				// While the game plays the keyboard is the game's: only E (edit mode), Escape and the F keys stay the
				// editor's
				if (raytracingScene->IsPlaying() && key != SDL_SCANCODE_E && key != SDL_SCANCODE_ESCAPE
					&& !(key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12)) {
					return;
				}

				switch (key) {
				case SDL_SCANCODE_E:
					SetEditMode(!editMode);
					break;

				// Delete the selected entity (and everything under it)
				case SDL_SCANCODE_DELETE:
					if (editMode && gizmo.GetSelected()) {
						raytracingScene->DestroyEntity(gizmo.GetSelected());
						gizmo.Select({});
					}
					break;

				// Play mode: play or stop (everything goes back as it was), pause, step a frame, the game's camera
				case SDL_SCANCODE_F5:
					TogglePlay();
					break;
				case SDL_SCANCODE_F6:
					if (raytracingScene->IsPlaying()) {
						raytracingScene->SetPaused(!raytracingScene->GetClock().paused);
						std::cout << (raytracingScene->GetClock().paused ? "Play: paused\n" : "Play: resumed\n");
					}
					break;
				case SDL_SCANCODE_F7:
					if (raytracingScene->IsPlaying()) {
						raytracingScene->StepFrame();
					}
					break;
				case SDL_SCANCODE_F8:
					if (raytracingScene->IsPlaying()) {
						cameraEjected = !cameraEjected;
						std::cout << (cameraEjected ? "Play: free camera (F8 looks through the game's camera again)\n" : "Play: the game's camera\n");
						ResetHistory();
					}
					break;

				case SDL_SCANCODE_P: {
					auto& cameraManager = raytracingScene->GetCameraManager();
					const bool playing = cameraManager.TogglePlay();
					if (playing) {
						std::cout << "Camera path: playing " << cameraManager.ActivePath()->name << "\n";
					}
					else if (!cameraManager.ActivePath() || !cameraManager.ActivePath()->IsFlyable()) {
						std::cout << "Camera path: none to play (edit mode: K adds keys from the view)\n";
					}
					else {
						std::cout << "Camera path: stopped\n";
					}
					UpdateWindowTitle(0.0f);
					break;
				}

				// Terrain brush on / off, and its size
				case SDL_SCANCODE_B:
					if (editMode) {
						terrainEditor.ToggleBrush();
						if (terrainEditor.IsBrushActive()) {
							shell.SetOpen(Fox::Editor::EditorShell::Window::Terrain, true); // The brush works with its window
						}
					}
					break;

				// Mesh paint brush on / off
				case SDL_SCANCODE_M:
					if (editMode) {
						meshPainter.Toggle();
						if (meshPainter.IsActive()) {
							shell.SetOpen(Fox::Editor::EditorShell::Window::MeshPaint, true);
						}
					}
					break;
				case SDL_SCANCODE_LEFTBRACKET:
				case SDL_SCANCODE_RIGHTBRACKET:
					if (editMode) {
						// The brush that is on
						if (meshPainter.IsActive()) {
							meshPainter.ScaleBrush(key == SDL_SCANCODE_RIGHTBRACKET ? 1.25f : 0.8f);
						}
						else {
							terrainEditor.ScaleBrush(key == SDL_SCANCODE_RIGHTBRACKET ? 1.25f : 0.8f);
						}
					}
					break;

				// A camera path key where the camera is
				case SDL_SCANCODE_K:
					if (editMode) {
						auto& camera = Managers::SceneManager::Get().GetCurrentScene()->GetMainCamera();
						cameraEditor.AddKeyFromView(raytracingScene->GetCameraManager(), *camera);
					}
					break;

				// Gizmo modes
				case SDL_SCANCODE_Z:
				case SDL_SCANCODE_X:
				case SDL_SCANCODE_C:
					if (editMode) {
						gizmo.SetMode(key == SDL_SCANCODE_Z ? Mode::Translate : key == SDL_SCANCODE_X ? Mode::Rotate : Mode::Scale);
						UpdateWindowTitle(0.0f);
					}
					break;

				case SDL_SCANCODE_N:
					SelectDenoiser(activeDenoiser + 1);
					break;

				case SDL_SCANCODE_V:
					vsync = !vsync;
					std::cout << "Vsync: " << (vsync ? "on" : "off") << "\n";
					Resize(0, 0);
					UpdateWindowTitle(0.0f);
					break;

				// Render resolution relative to the display: quality against frame rate
				case SDL_SCANCODE_U:
					SetUpscalingMode(static_cast<RayTracing::UpscalingMode>(
						(static_cast<int>(upscalingMode) + 1) % static_cast<int>(RayTracing::UpscalingMode::Count)));
					break;

				// Exposure, in thirds of a stop like a camera dial
				case SDL_SCANCODE_1:
				case SDL_SCANCODE_2:
				{
					float& exposure = raytracingScene->GetEnvironment().image.exposure;
					exposure += (key == SDL_SCANCODE_2 ? 1.0f : -1.0f) / 3.0f;
					std::cout << "Exposure: " << exposure << " EV\n";
				}
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
			}

		}
	}
}
