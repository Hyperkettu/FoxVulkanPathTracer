#include "FoxRenderer.h"
#include "FoxVulkanPathTracer/Graphics/Vulkan/CommandList.h"

PFN_vkCmdDrawMeshTasksEXT Fox::Graphics::Vulkan::CommandList::vkCmdDrawMeshTasksEXT = nullptr;
PFN_vkCmdTraceRaysKHR Fox::Graphics::Vulkan::CommandList::vkCmdTraceRaysKHR = nullptr;
PFN_vkSetDebugUtilsObjectNameEXT Fox::Graphics::Vulkan::CommandList::vkSetDebugUtilsObjectNameEXT = nullptr;
PFN_vkDebugMarkerSetObjectNameEXT Fox::Graphics::Vulkan::CommandList::vkGetObjectNameEXT = nullptr;

Fox::Graphics::Vulkan::CommandList& Fox::Graphics::Vulkan::CommandList::BuildAccelerationStructures(Fox::Scene::RayTracing::RayTracingScene& raytracingScene) {
    raytracingScene.BuildBLAS(cmd);
    raytracingScene.BuildTLAS(cmd);
    return *this;
}