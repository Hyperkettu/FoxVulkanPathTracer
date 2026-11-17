#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <stdexcept>
#include <glm/glm.hpp>

#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/RaytracingInstance.h"

namespace Fox::Graphics::Vulkan::RayTracing {
    class SkinningPass;
}


namespace Fox {
    namespace Scene {
        namespace RayTracing {

            class RayTracingScene {
            public:
                RayTracingScene(
                    VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    VkQueue queue,
                    uint32_t queueFamilyIndex);

                ~RayTracingScene();

                virtual void Initialize() = 0;
                virtual void Update(float dt) = 0;

                // Places the mesh in the scene as a movable object. The same mesh can be added several times with
                // different transforms: its geometry and acceleration structures are stored once and instanced.
                void AddMesh(const Fox::Graphics::Geometry::Vulkan::Mesh& mesh, const glm::mat4& transform, const std::string& name = "Mesh");

                // Adds a large horizontal quad at the given height, e.g. as the ground. Not movable.
                void AddGroundPlane(float height, float halfSize, const Fox::Graphics::Vulkan::Material& material);

                // Adds a sphere as a movable object, e.g. a glass ball (a material with transmission and an ior,
                // see Material.h). segments: subdivisions around the equator; it is smooth shaded either way.
                void AddSphere(const glm::vec3& center, float radius, const Fox::Graphics::Vulkan::Material& material,
                    const std::string& name = "Sphere", uint32_t segments = 128);

                // --- Skinned meshes: vertices follow joint matrices that can change every frame ---

                // Adds a skinned mesh as a movable object; returns its index for SetJointMatrices
                size_t AddSkinnedMesh(const Fox::Graphics::Geometry::SkinnedMesh& mesh, const Fox::Graphics::Vulkan::Material& material,
                    const glm::mat4& transform, const std::string& name);

                // Per joint of the mesh: pose * inverse(bind), in mesh space. Applied with the next RecordFrameUpdate,
                // which deforms the mesh on the GPU and refits its acceleration structure.
                void SetJointMatrices(size_t skinnedMesh, const std::vector<glm::mat4>& skinMatrices);

                // The object a skinned mesh is placed as (for GetObjectTransform)
                size_t GetObjectOfSkinnedMesh(size_t skinnedMesh) const { return skinnedMeshes[skinnedMesh].object; }

                // Bind pose of a skinned vertex, mirrors SkinVertex in Shaders/skinning.slang
                struct SkinVertex {
                    float position[4];
                    float normal[4];
                    float tangent[4];
                    uint32_t joints[4];
                    float weights[4];
                };

                // 3x4 row major, mirrors JointMatrix in Shaders/skinning.slang
                struct JointMatrix {
                    glm::vec4 rows[3];
                };

                // Where skinned vertices were in the previous frame (mesh space), for motion vectors
                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<glm::vec4>> previousPositionsSSBO;

                // --- Objects: everything placed in the scene, movable while it renders ---

                struct ObjectTransform {
                    glm::vec3 position{ 0.0f };
                    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
                    glm::vec3 scale{ 1.0f };

                    glm::mat4 Matrix() const {
                        return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
                    }
                };

                size_t GetObjectCount() const { return placements.size(); }
                const std::string& GetObjectName(size_t object) const { return placements[object].name; }
                bool IsObjectMovable(size_t object) const { return placements[object].movable; }
                const ObjectTransform& GetObjectTransform(size_t object) const { return placements[object].transform; }

                // Takes effect with the next RecordFrameUpdate
                void SetObjectTransform(size_t object, const ObjectTransform& transform);

                // The object an acceleration structure instance belongs to (InstanceIndex() in the shaders), -1 if none
                int GetObjectOfInstance(uint32_t instanceIndex) const {
                    return instanceIndex < instancePlacements.size() ? static_cast<int>(instancePlacements[instanceIndex]) : -1;
                }

                // Records what moved objects need on the GPU before this frame is traced: the top level
                // acceleration structure, the lights they carry and the motion of every instance since the
                // previous frame (instanceMotionSSBO, for motion vectors). frameSlot: the frame in flight.
                void RecordFrameUpdate(VkCommandBuffer cmd, uint32_t frameSlot);

                // Per acceleration structure instance: world space now -> world space in the previous frame
                struct InstanceMotion {
                    glm::vec4 rows[3]; // 3x4 matrix, row major
                };
                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<InstanceMotion>> instanceMotionSSBO;

                // Adds a sun: a distant light with soft shadows.
                //   directionToSun: where the sun is in the sky (does not need to be normalized)
                //   irradiance:     light arriving at a surface facing the sun
                //   angularRadius:  apparent radius of the disc in degrees (the real sun: 0.27). Larger = softer shadows
                void AddSun(const glm::vec3& directionToSun, const glm::vec3& irradiance, float angularRadius);

                // Uploads the geometry added so far and builds the acceleration structures.
                // Call once, after the last AddMesh.
                void Build();

                // Number of analytic lights (emissive quads, directional lights). Can be zero.
                uint32_t GetLightCount() const { return lightCount; }

                // Instance visibility bits, must match the masks in Shaders/common.slang
                static constexpr uint32_t INSTANCE_MASK_OPAQUE = 0x01;
                static constexpr uint32_t INSTANCE_MASK_GLASS = 0x02;       // Thin glass (windows)
                static constexpr uint32_t INSTANCE_MASK_SOLID_GLASS = 0x04; // Refracting solids

                inline std::vector<Fox::Graphics::Vulkan::Vertex>& GetVertices() {
                    return vertices;
                }

                inline std::vector<uint32_t>& GetIndices() {
                    return indices;
                }

                inline std::vector<Fox::Graphics::Vulkan::Submesh>& GetSubmeshes() {
                    return submeshes;
                }

                // One BLAS holding submeshes [firstSubmesh, firstSubmesh + submeshCount), one geometry each
                uint32_t AddBLAS(
                    VkDeviceAddress vertexAddress,
                    VkDeviceAddress indexAddress,
                    uint32_t firstSubmesh,
                    uint32_t submeshCount,
                    bool dynamic = false);

                void AddInstance(
                    uint32_t blasIndex,
                    const glm::mat4& transform,
                    uint32_t instanceCustomIndex,
                    uint32_t mask = 0xFF);

                void AddInstance(
                    uint32_t blasIndex,
                    const VkTransformMatrixKHR& transform,
                    uint32_t instanceCustomIndex,
                    uint32_t mask = 0xFF);

                void BuildBLAS(VkCommandBuffer cmd);
                void BuildTLAS(VkCommandBuffer cmd);

                static void RegisterExtensionFunctions(VkDevice device) {
                    vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));
                    vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));
                    vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR"));
                    vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));
                    vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR"));
                }

                VkAccelerationStructureKHR GetTLAS() const { return topLevelAccelerationStructure.handle; }
                VkDeviceAddress GetTLASAddress() const { return topLevelAccelerationStructure.deviceAddress; }

                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Vulkan::Vertex>> vertexSSBO;
                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<uint32_t>> indexSSBO;
                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Vulkan::Submesh>> submeshSSBO;
                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Vulkan::Material>> materialsSSBO; 
                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<Fox::Graphics::Vulkan::Light>> lightsSSBO;

                static uint32_t FindDeviceLocalMemoryType(
                    VkPhysicalDevice physicalDevice,
                    uint32_t typeFilter);

            protected:
                VkDevice device;
                VkPhysicalDevice physicalDevice;
                VkQueue queue;
                uint32_t queueFamily;

                std::vector<BottomLevelAccelerationStructure> bottomLevelAccelerationStructure;
                TopLevelAccelerationStructure topLevelAccelerationStructure;

                std::vector<VkAccelerationStructureInstanceKHR> instances;
                std::vector<uint32_t> instanceBlasIndices;
                std::unique_ptr<Fox::Graphics::Vulkan::Buffer> instanceBuffer;

                std::unique_ptr<Fox::Graphics::Vulkan::CommandPool> commandPool;


                VkBuffer scratchBuffer = VK_NULL_HANDLE;
                VkDeviceMemory scratchMemory = VK_NULL_HANDLE;

                std::vector<VkBuffer> scratchBuffers;
                std::vector<VkDeviceMemory> scratchBufferMemories;

                std::vector<Fox::Graphics::Vulkan::Vertex> vertices;
                std::vector<uint32_t> indices;  
                std::vector<Fox::Graphics::Vulkan::Submesh> submeshes;
                std::vector<Fox::Graphics::Vulkan::Material> materials;
                std::vector<Fox::Graphics::Vulkan::Light> lights; 

            private:
                void CreateAccelerationStructure(
                    VkAccelerationStructureTypeKHR type,
                    VkDeviceSize size,
                    VkAccelerationStructureKHR& as,
                    VkBuffer& buffer,
                    VkDeviceMemory& memory);

                void UploadInstanceBuffer();
                void UploadGeometry();

                // Transforms vertices [first, first + count): positions, normals and tangents
                void TransformVertices(uint32_t first, uint32_t count, const glm::mat4& transform);

                // Appends a mesh's geometry and materials (once per mesh) and returns its record
                size_t AddMeshGeometry(const Fox::Graphics::Geometry::Vulkan::Mesh* mesh);
                VkDeviceAddress GetBufferAddress(VkBuffer buffer);

                static PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
                static PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
                static PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
                static PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
                static PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;

                std::vector<std::unique_ptr<Fox::Graphics::Vulkan::VertexBuffer>> vertexBuffers;
                std::vector<std::unique_ptr<Fox::Graphics::Vulkan::IndexBuffer>> indexBuffers;

                uint32_t lightCount = 0;

                struct SkinnedRecord {
                    size_t object = 0;
                    size_t record = 0;
                    uint32_t firstVertex = 0;     // In the scene's vertex buffers
                    uint32_t vertexCount = 0;
                    uint32_t firstSkinVertex = 0; // In bindPose / previous positions
                    uint32_t firstJoint = 0;
                    std::vector<JointMatrix> joints;
                };
                std::vector<SkinnedRecord> skinnedMeshes;
                std::vector<SkinVertex> bindPose;
                uint32_t jointCount = 0;

                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<SkinVertex>> bindPoseSSBO;
                std::unique_ptr<Fox::Graphics::Vulkan::ShaderStorageBuffer<JointMatrix>> jointsSSBO;
                std::unique_ptr<Fox::Graphics::Vulkan::RayTracing::SkinningPass> skinningPass;
                VkDeviceAddress blasScratchAddress = 0; // Also used to refit deforming structures

                void RecordSkinning(VkCommandBuffer cmd);

                // Geometry stored once per mesh. Its submeshes are contiguous and grouped by kind (opaque,
                // thin glass, solid glass), each group one BLAS. Each kind has its own visibility bit, so shadow
                // rays can treat them differently (see IsOccluded in raygen.slang).
                enum SurfaceKind { Opaque = 0, ThinGlass, SolidGlass, SurfaceKindCount };

                struct MeshRecord {
                    const Fox::Graphics::Geometry::Vulkan::Mesh* mesh = nullptr; // nullptr for generated geometry
                    uint32_t firstSubmesh = 0;
                    uint32_t count[SurfaceKindCount] = {};
                    int blas[SurfaceKindCount] = { -1, -1, -1 };
                    bool dynamic = false; // Skinned: its acceleration structures are refitted every frame

                    uint32_t FirstOf(int kind) const {
                        uint32_t first = firstSubmesh;
                        for (int k = 0; k < kind; k++) first += count[k];
                        return first;
                    }
                };

                static SurfaceKind KindOf(const Fox::Graphics::Vulkan::Material& material) {
                    return material.IsSolidGlass() ? SolidGlass : material.IsThinGlass() ? ThinGlass : Opaque;
                }

                // A record for generated geometry with one submesh and one material, and its placement
                void AddGeneratedSurface(const std::vector<Fox::Graphics::Vulkan::Vertex>& surfaceVertices,
                    const std::vector<uint32_t>& surfaceIndices, const Fox::Graphics::Vulkan::Material& material,
                    const glm::mat4& transform, const std::string& name, bool movable);
                std::vector<MeshRecord> meshRecords;

                // Where each mesh is placed (an object); one TLAS instance per placement and BLAS
                struct Placement {
                    size_t record;
                    ObjectTransform transform;
                    std::string name;
                    bool movable = true;

                    // Its lights: entries [firstLight, firstLight + localLights.size()) of the light buffer
                    uint32_t firstLight = 0;
                    std::vector<Fox::Graphics::Vulkan::Light> localLights;

                    std::vector<uint32_t> instances;  // Its TLAS instances
                    glm::mat4 previousMatrix{ 1.0f }; // As traced in the previous frame
                };
                std::vector<Placement> placements;
                std::vector<uint32_t> instancePlacements; // Per TLAS instance

                static ObjectTransform Decompose(const glm::mat4& matrix);
                static Fox::Graphics::Vulkan::Light TransformLight(Fox::Graphics::Vulkan::Light light, const glm::mat4& transform);

                bool tlasDirty = false;
                bool lightsDirty = false;
                VkDeviceAddress tlasScratchAddress = 0;
                std::vector<std::unique_ptr<Fox::Graphics::Vulkan::Buffer>> frameInstanceBuffers; // Per frame in flight

                // Running offsets into the scene-wide arrays, advanced by AddMesh
                uint32_t vertexOffset = 0u;
                uint32_t indexOffset = 0u; 
                uint32_t materialOffset = 0u;
            };

        } // Raytracing
    } // Scene
} // Fox
