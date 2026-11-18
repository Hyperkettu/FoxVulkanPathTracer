#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <stdexcept>
#include <glm/glm.hpp>
#include <map>

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

                // Back to an empty scene: geometry, objects, lights and every GPU resource. Build() again after
                // adding the new content. Call with the device idle.
                void Clear();

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

                // --- Alpha testing (leaves) ---

                // CPU copy of the alpha channel of a texture (its bindless index). Alpha tested materials that use
                // the texture as base colour get opacity micromaps built from it (when the GPU supports them).
                void SetAlphaMask(int texture, Fox::Graphics::Vulkan::RayTracing::AlphaMask mask) { alphaMasks[texture] = std::move(mask); }
                bool HasAlphaMask(int texture) const { return alphaMasks.count(texture) > 0; }

                // Set by the renderer before the scene is created
                static void EnableOpacityMicromaps(bool enabled) { opacityMicromapsEnabled = enabled; }
                static bool OpacityMicromapsEnabled() { return opacityMicromapsEnabled; }

                // --- Skinned meshes: vertices follow joint matrices that can change every frame ---

                // Adds a skinned mesh as a movable object; returns its index for SetJointMatrices. One material per
                // part of the mesh (SkinnedMesh::parts; one material when it has no parts).
                size_t AddSkinnedMesh(const Fox::Graphics::Geometry::SkinnedMesh& mesh, const std::vector<Fox::Graphics::Vulkan::Material>& materials,
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

                // --- Dynamic meshes: replaced while the scene renders (trees from the editor) ---
                //
                // The scene's buffers are created with room for them after the static geometry, so replacing them only
                // rewrites that part: their vertices, indices, submeshes and materials, their skinning data, opacity
                // micromaps and bottom level acceleration structures. Static geometry and every descriptor stay as
                // they are (new textures aside). Call with the device idle.
                //
                // A mesh can be placed many times (instancing): it is stored, deformed and given a bottom level
                // structure once, and each placement is one more object (top level instance) using it.
                struct DynamicMesh {
                    const Fox::Graphics::Geometry::SkinnedMesh* mesh = nullptr;
                    std::vector<Fox::Graphics::Vulkan::Material> materials; // One per part
                    ObjectTransform transform;          // The first placement
                    std::string name;
                    std::vector<ObjectTransform> moreTransforms; // More placements of the same mesh
                    std::vector<std::string> moreNames;
                };
                struct DynamicMeshHandle {
                    size_t skinnedMesh = 0;         // For SetJointMatrices
                    size_t object = 0;              // The first placement, for GetObjectTransform / SetObjectTransform
                    std::vector<size_t> moreObjects; // The other placements, in the order of moreTransforms
                };
                // Throws std::runtime_error (scene unchanged) when the meshes do not fit the reserved room
                std::vector<DynamicMeshHandle> RebuildDynamicMeshes(const std::vector<DynamicMesh>& meshes);

                // --- Editable surfaces: generated geometry whose vertices change while the scene renders (terrain) ---
                //
                // Not movable. Its material comes first in the material buffer, then extraMaterials (a terrain's
                // layers: the surface material's layerMaterial is set to point at them). Returns the surface.
                size_t AddEditableSurface(const std::vector<Fox::Graphics::Vulkan::Vertex>& surfaceVertices,
                    const std::vector<uint32_t>& surfaceIndices, const Fox::Graphics::Vulkan::Material& material,
                    const std::vector<Fox::Graphics::Vulkan::Material>& extraMaterials, const glm::mat4& transform, const std::string& name);

                // New vertices [first, first + count) of a surface, uploaded with the next RecordFrameUpdate. Its
                // acceleration structure is refitted then (fast, but the tree gets looser as the shape changes);
                // RebuildSurface builds it anew instead, best once a stroke of edits is done.
                void UpdateSurfaceVertices(size_t surface, uint32_t first, const Fox::Graphics::Vulkan::Vertex* data, uint32_t count);
                void RebuildSurface(size_t surface);

                // The surface's material index (its extra materials follow it)
                uint32_t GetSurfaceMaterial(size_t surface) const { return editableSurfaces[surface].material; }
                // The surface's object (for its name; it cannot be moved)
                size_t GetSurfaceObject(size_t surface) const { return editableSurfaces[surface].object; }

                // Replaces a material in the scene's material buffer, uploaded with the next RecordFrameUpdate
                void SetMaterial(uint32_t index, const Fox::Graphics::Vulkan::Material& material);

                // Replaces texels of a texture (bindless index; 8 bit RGBA, tightly packed rows), uploaded with the
                // next RecordFrameUpdate
                void UpdateTextureRegion(int texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, std::vector<uint8_t> rgba);

                // Room reserved for dynamic meshes, and how much of it is in use
                static constexpr uint32_t DYNAMIC_VERTEX_CAPACITY = 2'500'000;
                static constexpr uint32_t DYNAMIC_INDEX_CAPACITY = 12'000'000;
                static constexpr uint32_t MAX_SUBMESHES = 65'536;
                static constexpr uint32_t MAX_MATERIALS = 4'096;
                static constexpr uint32_t MAX_INSTANCES = 16'384; // Painted forests: one per tree
                uint32_t GetDynamicVertexCount() const { return static_cast<uint32_t>(vertices.size()) - staticCounts.vertices; }
                uint32_t GetDynamicIndexCount() const { return static_cast<uint32_t>(indices.size()) - staticCounts.indices; }


                size_t GetObjectCount() const { return placements.size(); }
                const std::string& GetObjectName(size_t object) const { return placements[object].name; }
                bool IsObjectMovable(size_t object) const { return placements[object].movable; }
                const ObjectTransform& GetObjectTransform(size_t object) const { return placements[object].transform; }

                // Takes effect with the next RecordFrameUpdate
                void SetObjectTransform(size_t object, const ObjectTransform& transform);

                // Hidden objects stay in the scene but no ray hits them (instance mask 0) and their lights go dark.
                // Cheap: takes effect with the next RecordFrameUpdate, no rebuild.
                void SetObjectVisible(size_t object, bool visible);
                bool IsObjectVisible(size_t object) const { return placements[object].visible; }

                // Translation, rotation and scale of a matrix; shear is not supported
                static ObjectTransform Decompose(const glm::mat4& matrix);

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

                // Adds an analytic light (directional, point or spot; see Light.h) before Build; returns its index
                size_t AddLight(const Fox::Graphics::Vulkan::Light& light);
                // Replaces a light added with AddLight, uploaded with the next RecordFrameUpdate (no rebuild)
                void SetLight(size_t index, const Fox::Graphics::Vulkan::Light& light);

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

                // Opacity micromaps of alpha tested submeshes, attached to their geometries in AddBLAS. One set for
                // the static geometry, one replaced with the dynamic meshes.
                static bool opacityMicromapsEnabled;
                std::map<int, Fox::Graphics::Vulkan::RayTracing::AlphaMask> alphaMasks;
                struct OpacityMicromapSet {
                    std::unique_ptr<Fox::Graphics::Vulkan::RayTracing::OpacityMicromapBuilder> micromaps;
                    std::unique_ptr<Fox::Graphics::Vulkan::Buffer> indexBuffer;
                };
                OpacityMicromapSet staticMicromaps;
                OpacityMicromapSet dynamicMicromaps;
                struct OpacityGeometry {
                    VkAccelerationStructureTrianglesOpacityMicromapEXT info{};
                    std::vector<VkMicromapUsageEXT> usage;
                };
                std::map<uint32_t, std::unique_ptr<OpacityGeometry>> opacityGeometries; // By submesh
                void BuildOpacityMicromaps(uint32_t firstSubmesh, OpacityMicromapSet& set);

                // How much of each array is static geometry (set by the first upload)
                struct StaticCounts {
                    uint32_t vertices = 0, indices = 0, submeshes = 0, materials = 0;
                    size_t records = 0, placements = 0, blas = 0, instances = 0;
                    size_t skinnedMeshes = 0, bindPose = 0;
                    uint32_t joints = 0;
                };
                StaticCounts staticCounts;

                // Editable surfaces and their pending uploads (RecordEdits)
                struct EditableSurface {
                    size_t record = 0;
                    size_t object = 0;
                    uint32_t firstVertex = 0; // In the scene's vertex buffers
                    uint32_t vertexCount = 0;
                    uint32_t material = 0;
                    uint32_t dirtyBegin = UINT32_MAX, dirtyEnd = 0; // Vertices to upload, surface relative
                    bool refit = false;
                    bool rebuild = false;
                };
                std::vector<EditableSurface> editableSurfaces;
                struct TextureUpdate {
                    int texture = -1;
                    uint32_t x = 0, y = 0, width = 0, height = 0;
                    std::vector<uint8_t> rgba;
                };
                std::vector<TextureUpdate> textureUpdates;
                std::vector<uint32_t> dirtyMaterials;
                std::vector<std::unique_ptr<Fox::Graphics::Vulkan::Buffer>> editStaging; // Per frame in flight
                std::vector<VkDeviceSize> editStagingSize;
                VkBuffer editScratchBuffer = VK_NULL_HANDLE;
                VkDeviceMemory editScratchMemory = VK_NULL_HANDLE;
                VkDeviceSize editScratchSize = 0;
                void RecordEdits(VkCommandBuffer cmd, uint32_t frameSlot);

                // Builds the bottom level structures from index first on (with their own scratch memory)
                void BuildDynamicBLAS(size_t first);
                VkBuffer dynamicScratchBuffer = VK_NULL_HANDLE;
                VkDeviceMemory dynamicScratchMemory = VK_NULL_HANDLE;
                VkDeviceSize dynamicScratchSize = 0;

                void CreateSkinningResources();
                void ReleaseGPUResources();
                void AddPlacementInstances(size_t placement);
                void DestroyBLAS(BottomLevelAccelerationStructure& blas);

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
                    std::vector<uint32_t> masks;      // Their visibility masks while visible
                    glm::mat4 previousMatrix{ 1.0f }; // As traced in the previous frame
                    bool visible = true;
                };
                std::vector<Placement> placements;
                std::vector<uint32_t> instancePlacements; // Per TLAS instance

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
