#pragma once

struct BottomLevelAccelerationStructure {
    // Vulkan objects
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress deviceAddress = 0;

    // Build info (must be preserved!)
    std::vector<VkAccelerationStructureGeometryKHR> geometries;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;

    // Cached counts
    std::vector<uint32_t> primitiveCounts = {};

    // Size info
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};

    // Deforming geometry (skinned meshes): built to allow updates, refitted every frame
    bool dynamic = false;
};

struct TopLevelAccelerationStructure {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VkDeviceAddress deviceAddress = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};
