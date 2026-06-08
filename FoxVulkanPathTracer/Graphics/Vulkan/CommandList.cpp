#include "FoxRenderer.h"

PFN_vkCmdDrawMeshTasksEXT Fox::Graphics::Vulkan::CommandList::vkCmdDrawMeshTasksEXT = nullptr;
PFN_vkCmdTraceRaysKHR Fox::Graphics::Vulkan::CommandList::vkCmdTraceRaysKHR = nullptr;
PFN_vkSetDebugUtilsObjectNameEXT Fox::Graphics::Vulkan::CommandList::vkSetDebugUtilsObjectNameEXT = nullptr;
PFN_vkDebugMarkerSetObjectNameEXT Fox::Graphics::Vulkan::CommandList::vkGetObjectNameEXT = nullptr;