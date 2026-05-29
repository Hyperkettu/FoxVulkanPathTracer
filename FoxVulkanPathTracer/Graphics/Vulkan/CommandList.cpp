#include "FoxRenderer.h"

PFN_vkCmdDrawMeshTasksEXT Fox::Graphics::Vulkan::CommandList::vkCmdDrawMeshTasksEXT = nullptr;
PFN_vkCmdTraceRaysKHR Fox::Graphics::Vulkan::CommandList::vkCmdTraceRaysKHR = nullptr;