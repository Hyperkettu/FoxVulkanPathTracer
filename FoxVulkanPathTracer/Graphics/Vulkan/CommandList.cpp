#include "FoxRenderer.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/CommandList.h"

PFN_vkCmdDrawMeshTasksEXT Fox::Graphics::Vulkan::CommandList::vkCmdDrawMeshTasksEXT = nullptr;
PFN_vkCmdTraceRaysKHR Fox::Graphics::Vulkan::CommandList::vkCmdTraceRaysKHR = nullptr;
PFN_vkSetDebugUtilsObjectNameEXT Fox::Graphics::Vulkan::CommandList::vkSetDebugUtilsObjectNameEXT = nullptr;
PFN_vkDebugMarkerSetObjectNameEXT Fox::Graphics::Vulkan::CommandList::vkGetObjectNameEXT = nullptr;
PFN_vkGetRayTracingShaderGroupStackSizeKHR Fox::Graphics::Vulkan::CommandList::vkGetRayTracingShaderGroupStackSizeKHR = nullptr;
PFN_vkCmdSetRayTracingPipelineStackSizeKHR Fox::Graphics::Vulkan::CommandList::vkCmdSetRayTracingPipelineStackSizeKHR = nullptr;


Fox::Graphics::Vulkan::CommandList& Fox::Graphics::Vulkan::CommandList::BuildAccelerationStructures(Fox::Scene::RayTracing::RayTracingScene& raytracingScene) {
    raytracingScene.BuildBLAS(cmd);
    raytracingScene.BuildTLAS(cmd);
    return *this;
}

Fox::Graphics::Vulkan::CommandList& Fox::Graphics::Vulkan::CommandList::SetRecursionStackSize(VkPipeline pipeline) {
	//uint32_t rayGenGroupIdx = 0;
	//uint32_t missGroupIdx = 1; // Reflection miss
	//uint32_t shadowMissGroupIdx = 2; // Shadow miss 
	//uint32_t chitGroupIdx = 2; // Closest Hit  

	//VkDeviceSize rayGenStackSize = vkGetRayTracingShaderGroupStackSizeKHR(
	//	device, pipeline, rayGenGroupIdx,
	//	VkShaderGroupShaderKHR::VK_SHADER_GROUP_SHADER_GENERAL_KHR);

	//// Query Miss Group Stack Size
	//VkDeviceSize missStackSize = vkGetRayTracingShaderGroupStackSizeKHR(
	//	device, pipeline, missGroupIdx,
	//	VkShaderGroupShaderKHR::VK_SHADER_GROUP_SHADER_GENERAL_KHR);

	//// Query Closest Hit Group Stack Size (Note the specific enum name)
	//VkDeviceSize chitStackSize = vkGetRayTracingShaderGroupStackSizeKHR(
	//	device, pipeline, chitGroupIdx,
	//	VkShaderGroupShaderKHR::VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR);

	//// Find the heaviest miss shader
	//VkDeviceSize maxMissStackSize = missStackSize; //std::max(missStackSize, 0);

	//// 3. Compute the official Vulkan recursive stack size
	//// Max Depth = 4
	//uint32_t maxDepth = 4;

	//// Vulkan Spec Formula: 
	//// Stack = RayGen + max(Miss, CHit + max(Secondary_Miss, Secondary_CHit)) * MaxDepth
	//// In our case, the Closest Hit shader can trigger another Closest Hit or a Shadow Ray.
	//VkDeviceSize maxCalculatedStack = rayGenStackSize +
	//	std::max(maxMissStackSize, chitStackSize) * maxDepth;

	//// 4. Set the stack size dynamically during your command recording
	//vkCmdSetRayTracingPipelineStackSizeKHR(cmd, maxCalculatedStack);

	uint32_t rayGenGroupIdx = 0;
	uint32_t missGroupIdx = 1; // Reflection miss
	uint32_t shadowMissGroupIdx = 3; // Shadow miss 
	uint32_t chitGroupIdx = 2; // Closest Hit  

	VkDeviceSize rayGenStackSize = vkGetRayTracingShaderGroupStackSizeKHR(
		device, pipeline, rayGenGroupIdx,
		VkShaderGroupShaderKHR::VK_SHADER_GROUP_SHADER_GENERAL_KHR);

	// Query Miss Group Stack Size
	VkDeviceSize missStackSize = vkGetRayTracingShaderGroupStackSizeKHR(
		device, pipeline, missGroupIdx,
		VkShaderGroupShaderKHR::VK_SHADER_GROUP_SHADER_GENERAL_KHR);

	VkDeviceSize shadowMissStackSize = vkGetRayTracingShaderGroupStackSizeKHR(
		device, pipeline, shadowMissGroupIdx,
		VkShaderGroupShaderKHR::VK_SHADER_GROUP_SHADER_GENERAL_KHR);

	// Query Closest Hit Group Stack Size (Note the specific enum name)
	VkDeviceSize chitStackSize = vkGetRayTracingShaderGroupStackSizeKHR(
		device, pipeline, chitGroupIdx,
		VkShaderGroupShaderKHR::VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR);

	// Find the heaviest miss shader
	VkDeviceSize maxMissStackSize = std::max(missStackSize, shadowMissStackSize); //std::max(missStackSize, 0);

	// 3. Compute the official Vulkan recursive stack size
	// Max Depth = 4
	uint32_t maxDepth = 4;

	// Vulkan Spec Formula: 
	// Stack = RayGen + max(Miss, CHit + max(Secondary_Miss, Secondary_CHit)) * MaxDepth
	// In our case, the Closest Hit shader can trigger another Closest Hit or a Shadow Ray.
	VkDeviceSize maxCalculatedStack = rayGenStackSize +
		std::max(maxMissStackSize, chitStackSize) * maxDepth;

	// 4. Set the stack size dynamically during your command recording
	vkCmdSetRayTracingPipelineStackSizeKHR(cmd, maxCalculatedStack);

	return *this;

}