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
				appInfo.pApplicationName = "Fox Vulkan Renderer";
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
				Fox::Scene::Raytracing::RayTracingScene::RegisterExtensionFunctions(device);

				raytracingPipeline = std::make_unique<Fox::Graphics::Vulkan::RayTracing::RayTracingPipeline>(device, physicalDevice);

				VkDescriptorSetLayoutBinding bindings[3]{};

				// TLAS
				bindings[0] = {
					.binding = 0,
					.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
					.descriptorCount = 1,
					.stageFlags =
						VK_SHADER_STAGE_RAYGEN_BIT_KHR |
						VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
				};

				// Output image
				bindings[1] = {
					.binding = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
				};

				bindings[2] = {
					.binding = 2,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.descriptorCount = 1,
					.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR
				};

				VkDescriptorSetLayoutCreateInfo layoutInfo{
					.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
					.bindingCount = 3,
					.pBindings = bindings
				};

				VkDescriptorSetLayout descriptorSetLayout;
				vkCreateDescriptorSetLayout(
					device, &layoutInfo, nullptr, &descriptorSetLayout);

				VkPipelineLayoutCreateInfo pipelineLayoutInfo{
					.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
					.setLayoutCount = 1,
					.pSetLayouts = &descriptorSetLayout
				};

				vkCreatePipelineLayout(
					device, &pipelineLayoutInfo, nullptr, &raytracingPipelineLayout);

				std::vector<VkPipelineShaderStageCreateInfo> stages;

				std::vector<char> raygenCode = Fox::Core::FileSystem::ReadBinaryFile("Shaders/raygen.spv");
				VkShaderModule raygenModule = CreateShaderModule(device, reinterpret_cast<uint32_t*>(raygenCode.data()), raygenCode.size());

				std::vector<char> missCode = Fox::Core::FileSystem::ReadBinaryFile("Shaders/miss.spv");
				VkShaderModule missModule = CreateShaderModule(device, reinterpret_cast<uint32_t*>(missCode.data()), missCode.size());

				std::vector<char> chitCode = Fox::Core::FileSystem::ReadBinaryFile("Shaders/closestHit.spv");
				VkShaderModule chitModule = CreateShaderModule(device, reinterpret_cast<uint32_t*>(chitCode.data()), chitCode.size());

				// RayGen
				stages.push_back({
					.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
					.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
					.module = raygenModule,
					.pName = "main"
					});

				// Miss
				stages.push_back({
					.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
					.stage = VK_SHADER_STAGE_MISS_BIT_KHR,
					.module = missModule,
					.pName = "main"
					});

				// Closest Hit
				stages.push_back({
					.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
					.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
					.module = chitModule,
					.pName = "main"
					});

				VkRayTracingShaderGroupCreateInfoKHR raygenGroup{
					.sType =
						VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
					.generalShader = 0,
					.closestHitShader = VK_SHADER_UNUSED_KHR,
					.anyHitShader = VK_SHADER_UNUSED_KHR,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				};

				VkRayTracingShaderGroupCreateInfoKHR missGroup{
					.sType =
						VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
					.generalShader = 1,
					.closestHitShader = VK_SHADER_UNUSED_KHR,
					.anyHitShader = VK_SHADER_UNUSED_KHR,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				};

				VkRayTracingShaderGroupCreateInfoKHR hitGroup{
					.sType =
						VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type =
						VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
					.generalShader = VK_SHADER_UNUSED_KHR,
					.closestHitShader = 2,
					.anyHitShader = VK_SHADER_UNUSED_KHR,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				};

				std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups = {
					raygenGroup,
					missGroup,
					hitGroup
				};

				raytracingPipeline->Create(
					raytracingPipelineLayout,
					stages,
					shaderGroups);

				raytracingScene = std::make_unique<Fox::Scene::Raytracing::RayTracingScene>(device, physicalDevice, graphicsQueue, graphicsQueueFamily);

				mesh = Fox::Graphics::Geometry::GeometryGenerator::GeneratePlaneMesh(1, 1, 100.0f, 100.0f, 0.0f);

				vertexBuffer = std::make_unique<Fox::Graphics::Vulkan::VertexBuffer>();
				vertexBuffer->Create(device, physicalDevice, frameResources[0]->commandPool->Get(), graphicsQueue, mesh.GetVertices());

				indexBuffer = std::make_unique<Fox::Graphics::Vulkan::IndexBuffer>();
				indexBuffer->Create(device, physicalDevice, frameResources[0]->commandPool->Get(), graphicsQueue, mesh.GetIndices());

				VkDeviceAddress vertexAddress = GetBufferDeviceAddress(vertexBuffer->Get());
				VkDeviceAddress indexAddress = GetBufferDeviceAddress(indexBuffer->Get());

				uint32_t vertexCount = mesh.GetVertices().size();
				uint32_t indexCount = mesh.GetIndices().size();

				uint32_t blasIndex = raytracingScene->AddBLAS(
					vertexBuffer->Get(),
					vertexAddress,
					vertexCount,
					indexBuffer->Get(),
					indexAddress,
					indexCount);

				glm::mat4 transform = glm::mat4(1.0f); // identity

				raytracingScene->AddInstance(
					blasIndex,
					transform,
					/*instanceCustomIndex=*/0,
					/*mask=*/0xFF);

				Fox::Graphics::Vulkan::CommandList cmdList(device, frameResources[0]->commandPool->Get());
				cmdList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
					.BuildAccelerationStructures(*raytracingScene.get())
					.End()
					.SubmitAndWait(graphicsQueue);


				storageImages.resize(config.MAX_FRAMES_IN_FLIGHT + 1);
				storageImageMemorys.resize(config.MAX_FRAMES_IN_FLIGHT  + 1);
				storageImageViews.resize(config.MAX_FRAMES_IN_FLIGHT + 1);


				for (auto i = 0; i < config.MAX_FRAMES_IN_FLIGHT + 1; i++) {

					VkImageCreateInfo imageInfo{
						.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
						.imageType = VK_IMAGE_TYPE_2D,
						.format = VK_FORMAT_R8G8B8A8_UNORM, // good default
						.extent = {
							.width = capabilities.currentExtent.width,
							.height = capabilities.currentExtent.height,
							.depth = 1
						},
						.mipLevels = 1,
						.arrayLayers = 1,
						.samples = VK_SAMPLE_COUNT_1_BIT,
						.tiling = VK_IMAGE_TILING_OPTIMAL,
						.usage =
							VK_IMAGE_USAGE_STORAGE_BIT |
							VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
							VK_IMAGE_USAGE_TRANSFER_DST_BIT,
						.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
						.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
					};

					vkCreateImage(device, &imageInfo, nullptr, &storageImages[i]);

					VkMemoryRequirements memReq;
					vkGetImageMemoryRequirements(device, storageImages[i], &memReq);

					uint32_t memoryTypeIndex = Buffer::FindMemoryType(
						physicalDevice,
						memReq.memoryTypeBits,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
					);

					VkMemoryAllocateInfo allocInfo{
						.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
						.allocationSize = memReq.size,
						.memoryTypeIndex = memoryTypeIndex
					};

					vkAllocateMemory(device, &allocInfo, nullptr, &storageImageMemorys[i]);
					vkBindImageMemory(device, storageImages[i], storageImageMemorys[i], 0);



					VkImageViewCreateInfo viewInfo{
						.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
						.image = storageImages[i],
						.viewType = VK_IMAGE_VIEW_TYPE_2D,
						.format = VK_FORMAT_R8G8B8A8_UNORM,
						.subresourceRange = {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.baseMipLevel = 0,
							.levelCount = 1,
							.baseArrayLayer = 0,
							.layerCount = 1
						}
					};

					vkCreateImageView(device, &viewInfo, nullptr, &storageImageViews[i]);

					VkImageMemoryBarrier barrier{
						.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
						.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
						.newLayout = VK_IMAGE_LAYOUT_GENERAL,
						.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
						.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
						.image = storageImages[i],
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
						.TransitionImageLayout(storageImages[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
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
					VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
				};

				VkPhysicalDeviceMaintenance4Features maintenance4Features = {
					.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES,
					.maintenance4 = VK_TRUE
				};

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

				// just pick first
				surfaceFormat = formats[0];

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

				auto frameResource = frameResources[currentFrame].get();

				frameResource->renderFence->Wait();
				frameResource->renderFence->Reset();

				std::array<VkClearValue, 2> clearValues{};
				clearValues[0].color = { 0.0f, 0.0f, 0.5f, 1.0f };
				clearValues[1].depthStencil = { 1.0f, 0 };

				frameResource->commandPool->Reset();
				
				uint32_t imageIndex = swapchain->AcquireNextImage(frameResource->imageAvailableSemaphore->Get());

				UpdateUniformBuffer(currentFrame);

			/*	auto offscreenPipeline = Fox::Graphics::Managers::Vulkan::PipelineManager::Get().GetPipeline(Fox::Graphics::Managers::Vulkan::PipelineCategory::OFFSCREEN_RENDERING)->Get();
				auto offscreenPipelineLayout = Fox::Graphics::Managers::Vulkan::PipelineManager::Get().GetPipelineLayout(Fox::Graphics::Managers::Vulkan::PipelineCategory::OFFSCREEN_RENDERING)->Get();


				frameResource->offscreenCommandList->Begin()
					.BeginRenderPass(Fox::Graphics::Managers::Vulkan::RenderPassManager::Get().GetPass(Fox::Graphics::Managers::Vulkan::RenderPass::OFFSCREEN)->Get(), 
						offscreenTarget->Get(), capabilities.currentExtent, &clearValues[0], 2)
					.SetViewport(0, 0, capabilities.currentExtent.width, capabilities.currentExtent.height)
					.SetScissor(0, 0, capabilities.currentExtent.width, capabilities.currentExtent.height)
					.BindPipeline(offscreenPipeline)
					.BindDescriptorSets(offscreenPipelineLayout, 0, { frameResource->offscreenDescriptorSet->Get() })
					.RenderMeshShader(1, 1, 1)
					.EndRenderPass()
					.End()
					.Submit(graphicsQueue, frameResource->imageAvailableSemaphore->Get(), frameResource->offscreenFinishedSemaphore->Get(), VK_NULL_HANDLE);

				std::array<VkClearValue, 2> clearValuesPost{};
				clearValuesPost[0].color = { 1.0f, 0.0f, 1.0f, 1.0f };
				clearValuesPost[1].depthStencil = { 1.0f, 0 };

				auto basicMeshShaderPipeline = Fox::Graphics::Managers::Vulkan::PipelineManager::Get().GetPipeline(Fox::Graphics::Managers::Vulkan::PipelineCategory::BASIC_MESH_SHADER)->Get();
				auto basicMeshShaderPipelineLayout = Fox::Graphics::Managers::Vulkan::PipelineManager::Get().GetPipeline(Fox::Graphics::Managers::Vulkan::PipelineCategory::BASIC_MESH_SHADER)->GetLayout();

				frameResource->commandList->Begin()
					.BeginRenderPass(Fox::Graphics::Managers::Vulkan::RenderPassManager::Get().GetPass(Fox::Graphics::Managers::Vulkan::RenderPass::DEFAULT)->Get(), 
						swapchain->GetFramebuffer(imageIndex), capabilities.currentExtent, &clearValuesPost[0], 2)
					.SetViewport(0, 0, capabilities.currentExtent.width, capabilities.currentExtent.height)
					.SetScissor(0, 0, capabilities.currentExtent.width, capabilities.currentExtent.height)
					.BindPipeline(basicMeshShaderPipeline)
					.BindDescriptorSets(basicMeshShaderPipelineLayout, 0, { frameResource->perFrameDescriptorSet->Get() })
					.BindVertexBuffers(0, { scene->GetVertexBuffer()->Get()}, {0})
					.BindIndexBuffer(scene->GetIndexBuffer()->Get(), 0, VkIndexType::VK_INDEX_TYPE_UINT32)
					.RenderMeshShader(scene->GetSceneMeshInfos().size(), 1, 1)
					.EndRenderPass()
					.End()
					.Submit(graphicsQueue, frameResource->offscreenFinishedSemaphore->Get(), frameResource->renderFinishedSemaphore->Get(), frameResource->renderFence->Get());
				*/

				std::array<VkClearValue, 2> clearValuesPost{}; 
				clearValuesPost[0].color = { 1.0f, 0.0f, 1.0f, 1.0f };
				clearValuesPost[1].depthStencil = { 1.0f, 0 };

				VkImageSubresourceRange subresourceRange = {
						VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
				};

				frameResource->commandList->Begin() 
				//	.BeginRenderPass(Fox::Graphics::Managers::Vulkan::RenderPassManager::Get().GetPass(Fox::Graphics::Managers::Vulkan::RenderPass::DEFAULT)->Get(), 
				//		swapchain->GetFramebuffer(imageIndex), capabilities.currentExtent, &clearValuesPost[0], 2) 
					.SetViewport(0, 0, capabilities.currentExtent.width, capabilities.currentExtent.height) 
					.SetScissor(0, 0, capabilities.currentExtent.width, capabilities.currentExtent.height) 
					.BindPipeline(raytracingPipeline->GetPipeline(), VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
					.BindDescriptorSets(raytracingPipelineLayout, 0, { frameResource->perFrameDescriptorSet->Get() }, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
					.Raytrace(
						raytracingPipeline->GetRaygenRegion(),
						raytracingPipeline->GetMissRegion(),
						raytracingPipeline->GetHitRegion(), 
						raytracingPipeline->GetCallableRegion(), capabilities.currentExtent.width, capabilities.currentExtent.height)
				//	.EndRenderPass()
					.TransitionImageLayout(storageImages[currentFrame], VkImageLayout::VK_IMAGE_LAYOUT_GENERAL,
						VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresourceRange, VkPipelineStageFlagBits::VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT)
					.TransitionImageLayout(swapchain->GetImage(imageIndex), VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
						VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange, VkPipelineStageFlagBits::VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT)

					.CopyImage(storageImages[currentFrame], swapchain->GetImage(imageIndex), capabilities.currentExtent.width, capabilities.currentExtent.height)
					.TransitionImageLayout(swapchain->GetImage(imageIndex), VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, subresourceRange, 
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT, VkPipelineStageFlagBits::VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
					.TransitionImageLayout(storageImages[currentFrame], VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VkImageLayout::VK_IMAGE_LAYOUT_GENERAL, subresourceRange,
						VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT, VkPipelineStageFlagBits::VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR)
					.End()
					.Submit(graphicsQueue, frameResource->imageAvailableSemaphore->Get(), frameResource->renderFinishedSemaphore->Get(), frameResource->renderFence->Get());

				Present(swapchain, imageIndex, frameResource->renderFinishedSemaphore);

				currentFrame = (currentFrame + 1) % config.MAX_FRAMES_IN_FLIGHT;

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
				Fox::Graphics::Managers::Vulkan::PipelineManager::Get().Initialize(device, capabilities);
				std::cout << "Graphics pipeline successfully created" << std::endl;
				return 1;
			}

			void VulkanRHI::LoadExtensionAPIFunctions() {
				Fox::Graphics::Vulkan::CommandList::RegisterRenderMeshShaderFunction(
					reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(vkGetDeviceProcAddr(device, "vkCmdDrawMeshTasksEXT")),
					reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR")));
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
					frameResource->perFrameDescriptorSet = std::make_unique<Fox::Graphics::Vulkan::DescriptorSet>(descriptorManager.GetDescriptorSet(Fox::Graphics::Managers::Vulkan::Descriptor::MAIN_MESH_SHADER)->AllocateSet());
					frameResource->offscreenDescriptorSet = std::make_unique<Fox::Graphics::Vulkan::DescriptorSet>(descriptorManager.GetDescriptorSet(Fox::Graphics::Managers::Vulkan::Descriptor::OFFSCREEN)->AllocateSet());

					frameResource->perFrameDescriptorSet->Reserve(2);

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
					cam.camForward = glm::vec3(0.0f, 0.0f, 1.0f);
					cam.camRight = glm::vec3(1.0f, 0.0f, 0.0f);
					cam.camUp = glm::vec3(0.0f, 1.0f, 0.0f);
					cam.resolution = glm::vec2(capabilities.currentExtent.width, capabilities.currentExtent.height);

					frameResource->camUBO->Update(cam);
				}

				for (uint32_t i = 0; i < config.MAX_FRAMES_IN_FLIGHT; i++) {

					/*	frameResources[i]->offscreenDescriptorSet->ClearWrites();
						frameResources[i]->offscreenDescriptorSet->SetConstantBuffer(0, frameResources[i]->oldPerFrameUBO);
						frameResources[i]->offscreenDescriptorSet->SetShaderResourceTexture(1, Fox::Graphics::Managers::Vulkan::TextureManager::Get().GetShaderResourceTexture(Fox::Graphics::Managers::Vulkan::ShaderResourceTexture::BOX).get());
						frameResources[i]->offscreenDescriptorSet->Update();

						frameResources[i]->perFrameDescriptorSet->ClearWrites();
						frameResources[i]->perFrameDescriptorSet->SetVertexBuffer<Vertex>(0, Graphics::Managers::Vulkan::SceneManager::Get().GetCurrentScene()->GetVertexBuffer());
						frameResources[i]->perFrameDescriptorSet->SetIndexBuffer<uint32_t>(1, Graphics::Managers::Vulkan::SceneManager::Get().GetCurrentScene()->GetIndexBuffer());
						frameResources[i]->perFrameDescriptorSet->SetConstantBuffer(2, frameResources[i]->perFrameUBO);
						frameResources[i]->perFrameDescriptorSet->SetDynamicConstantBuffer(3, frameResources[i]->meshTransformsUBO);
						frameResources[i]->perFrameDescriptorSet->SetDynamicStorageBuffer(4, frameResources[i]->meshInfosUBO);
						frameResources[i]->perFrameDescriptorSet->SetShaderResourceTexture(5, Fox::Graphics::Managers::Vulkan::TextureManager::Get().GetRenderTargetTexture(Fox::Graphics::Managers::Vulkan::RenderTargetTexture::DEFAULT_RENDER_TARGET).get());
						frameResources[i]->perFrameDescriptorSet->Update();
						*/

					frameResources[i]->perFrameDescriptorSet->ClearWrites();
					auto tlas = raytracingScene->GetTLAS(); 
					frameResources[i]->perFrameDescriptorSet->SetAccelerationStructure(0, tlas);
					frameResources[i]->perFrameDescriptorSet->SetStorageImage(1, storageImageViews[i]);
					frameResources[i]->perFrameDescriptorSet->SetConstantBuffer(2, frameResources[i]->camUBO);
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

				rotationAngle += 0.005f;

				Fox::Graphics::Vulkan::OldFrame oldPerFrame{};
				oldPerFrame.model = glm::rotate(glm::mat4(1.0f),
						(float)rotationAngle * glm::radians(90.0f),
						glm::vec3(0.0f, 0.0f, 1.0f));
				oldPerFrame.view = camera->GetViewMatrix();
				oldPerFrame.proj = camera->GetProjectionMatrix();

				frameResources[currentFrame]->oldPerFrameUBO->Update(oldPerFrame);

				Fox::Graphics::Vulkan::PerFrame perFrame{};
				perFrame.view = camera->GetViewMatrix();
				perFrame.proj = camera->GetProjectionMatrix();

				frameResources[currentFrame]->perFrameUBO->Update(perFrame);

				Fox::Graphics::Vulkan::MeshTransforms meshTransforms{};
				meshTransforms.models.push_back(glm::rotate(glm::mat4(1.0f),
					(float)rotationAngle * glm::radians(90.0f),
					glm::vec3(0.0f, 0.0f, 1.0f)));

				//meshTransforms.models.push_back(glm::rotate(glm::mat4(1.0f),
				//	(float)rotationAngle * glm::radians(45.0f),
				//	glm::vec3(0.0f, 0.0f, 1.0f)) * glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f,0.0f)));

				//meshTransforms.models.push_back(glm::rotate(glm::mat4(1.0f),
				//	(float)rotationAngle * glm::radians(60.0f),
				//	glm::vec3(0.0f, 0.0f, 1.0f)) * glm::translate(glm::mat4(1.0f), glm::vec3(8.0f, 0.0f, 0.0f)));

				Fox::Graphics::Vulkan::CameraUBO cam;
				cam.camPos = camera->GetPosition();
				cam.camForward = glm::vec3(0.0f, 0.0f, 1.0f); //camera->GetForward(); // glm::vec3(0.0f, 0.0f, 1.0f);
				cam.camRight = glm::vec3(1.0f, 0.0f, 0.0f); //camera->GetRight(); //glm::vec3(1.0f, 0.0f, 0.0f);
				cam.camUp = glm::vec3(0.0f, 1.0f, 0.0f); //camera->GetUp(); //glm::vec3(0.0f, 1.0f, 0.0f);
				cam.resolution = glm::vec2(capabilities.currentExtent.width, capabilities.currentExtent.height);

				std::cout << camera->GetPosition().x << " " << camera->GetPosition().y << " " << camera->GetPosition().z << std::endl;

				frameResources[currentFrame]->camUBO->Update(cam);


				frameResources[currentFrame]->meshInfosUBO->Update(scene->GetSceneMeshInfos()); 
				frameResources[currentFrame]->meshTransformsUBO->Update(meshTransforms.models);
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