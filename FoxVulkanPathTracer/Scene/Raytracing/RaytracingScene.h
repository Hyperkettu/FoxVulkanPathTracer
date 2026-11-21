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

                // Simulations that write geometry (water) record their work here, before the structures are
                // updated (RecordFrameUpdate calls it)
                virtual void RecordSimulations(VkCommandBuffer cmd, uint32_t frameSlot) {}

                // Places the mesh in the scene as a movable object. The same mesh can be added several times with
                // different transforms: its geometry and acceleration structures are stored once and instanced.
                void AddMesh(const Fox::Graphics::Geometry::Vulkan::Mesh& mesh, const glm::mat4& transform, const std::string& name = "Mesh");

                // Instanced objects (clutter: grass, flowers, stones): one object, many copies of one mesh, each copy its
                // own top level instance sharing the mesh's structures. items: each copy's matrix (world space as placed;
                // the object's transform then moves them all). Copies past the clutter budget (MAX_CLUTTER_INSTANCES top
                // level instances in all) are left out: GetObjectItems tells how many were placed. No light sampling of
                // emissive clutter (it still glows where rays hit it). Before Build.
                size_t AddInstancedMesh(const Fox::Graphics::Geometry::Vulkan::Mesh& mesh, std::vector<glm::mat4> items, const std::string& name);
                size_t AddInstancedSurface(const std::vector<Fox::Graphics::Vulkan::Vertex>& surfaceVertices, const std::vector<uint32_t>& surfaceIndices,
                    const Fox::Graphics::Vulkan::Material& material, std::vector<glm::mat4> items, const std::string& name);
                const std::vector<glm::mat4>& GetObjectItems(size_t object) const { return placements[object].items; }
                // To move copies (the same count): traced from the next frame
                std::vector<glm::mat4>& EditObjectItems(size_t object) {
                    tlasDirty = true;
                    return placements[object].items;
                }

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

                // Cluster level of detail for skinned meshes that come with one (trees); set by the renderer
                static void EnableClusterLod(bool enabled) { clusterLodEnabled = enabled; }
                static bool ClusterLodEnabled() { return clusterLodEnabled; }

                // Where the clusters are seen from, before each frame's RecordFrameUpdate. pixelScale: image height
                // / (2 tan(vertical fov / 2)); threshold: pixels of geometric error allowed (larger: coarser, faster)
                const Fox::Graphics::Vulkan::RayTracing::ClusterLodPass::View& GetClusterLodView() const { return clusterView; }
                void SetClusterLodView(const glm::vec3& cameraPosition, float pixelScale) {
                    clusterView.cameraPosition = cameraPosition;
                    clusterView.pixelScale = pixelScale;
                }
                void SetClusterLodThreshold(float pixels) { clusterThreshold = std::max(pixels, 0.05f); }
                float GetClusterLodThreshold() const { return clusterThreshold; }
                bool HasClusterMeshes() const { return !clusterMeshes.empty(); }
                // A frame or two old
                Fox::Graphics::Vulkan::RayTracing::ClusterLodPass::Stats GetClusterLodStats() const {
                    return clusterPass ? clusterPass->GetStats() : Fox::Graphics::Vulkan::RayTracing::ClusterLodPass::Stats{};
                }
                void ReadClusterLodStats(uint32_t frameSlot) {
                    if (!clusterPass) return;
                    clusterPass->ReadStats(frameSlot);
                    // Near the instance structures' budget: coarser everywhere for a while; back as there is room again
                    const float used = static_cast<float>(clusterPass->GetStats().clustersSelected) / static_cast<float>(std::max(clusterPass->GetBottomLevelBudget(), 1u));
                    if (used > 0.85f) clusterOverload = std::min(clusterOverload * 1.5f, 1024.0f);
                    else if (used < 0.5f) clusterOverload = std::max(clusterOverload / 1.1f, 1.0f);
                }

                // --- Skinned meshes: vertices follow joint matrices that can change every frame ---

                // Adds a skinned mesh as a movable object; returns its index for SetJointMatrices. One material per
                // part of the mesh (SkinnedMesh::parts; one material when it has no parts).
                size_t AddSkinnedMesh(const Fox::Graphics::Geometry::SkinnedMesh& mesh, const std::vector<Fox::Graphics::Vulkan::Material>& materials,
                    const glm::mat4& transform, const std::string& name, const Fox::Graphics::Geometry::ClusterLod* lod = nullptr);

                // Per joint of the mesh: pose * inverse(bind), in mesh space. Applied with the next RecordFrameUpdate,
                // which deforms the mesh on the GPU and refits its acceleration structure.
                void SetJointMatrices(size_t skinnedMesh, const std::vector<glm::mat4>& skinMatrices);
                // Growing leaves (SkinnedMesh::growth): those whose stem is less than leafFront metres along the tree
                // have unfolded, over leafSpan metres (see Animation::TreeGrowth). 1e30: all grown.
                void SetSkinGrowth(size_t skinnedMesh, float leafFront, float leafSpan);
                // Whether the GPU deforms a skinned mesh this frame (wind level of detail):
                //   Deform  as its joints are now
                //   Hold    not this frame: it keeps last frame's shape (its motion vectors keep the last motion)
                //   Freeze  once more with the same joints (its motion catches up: none), then not until Deform
                enum class SkinUpdate { Deform, Hold, Freeze };
                void SetSkinUpdate(size_t skinnedMesh, SkinUpdate update);

                // The object a skinned mesh is placed as (for GetObjectTransform)
                size_t GetObjectOfSkinnedMesh(size_t skinnedMesh) const { return skinnedMeshes[skinnedMesh].object; }

                // Bind pose of a skinned vertex, mirrors SkinVertex in Shaders/skinning.slang
                struct SkinVertex {
                    float position[4];
                    float normal[4];
                    float tangent[4];
                    uint32_t joints[4];
                    float weights[4];
                    float growth[4]; // SkinnedMesh::growth: a leaf's stem and 1 + its distance along the tree (0: none)
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
                    // Its cluster level of detail (indexes mesh's vertices); traced through clusters when the GPU can
                    const Fox::Graphics::Geometry::ClusterLod* lod = nullptr;
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
                // Simulated surfaces written on the CPU (particles): where its vertices were last frame (motion vectors),
                // uploaded with the next RecordFrameUpdate
                void UpdateSurfacePrevious(size_t surface, const glm::vec4* data, uint32_t count);

                // The surface's material index (its extra materials follow it)
                uint32_t GetSurfaceMaterial(size_t surface) const { return editableSurfaces[surface].material; }

                // --- Simulated surfaces: vertices a GPU simulation writes every frame (water) ---
                //
                // An editable surface whose vertices (and their previous positions, for motion vectors) a compute
                // shader writes in RecordSimulations. Each frame it is deformed (MarkSurfaceDeformed) its structure
                // is refitted, and now and then built anew. Not movable. Returns the surface.
                size_t AddSimulatedSurface(const std::vector<Fox::Graphics::Vulkan::Vertex>& surfaceVertices,
                    const std::vector<uint32_t>& surfaceIndices, const Fox::Graphics::Vulkan::Material& material, const std::string& name);
                struct SimulatedRange {
                    uint32_t firstVertex = 0;   // In the scene's vertex buffers
                    uint32_t vertexCount = 0;
                    uint32_t firstPrevious = 0; // In the previous positions buffer
                };
                SimulatedRange GetSimulatedRange(size_t surface) const;
                // rebuild: built anew this frame (for geometry that moves too much to refit, e.g. particles)
                void MarkSurfaceDeformed(size_t surface, bool rebuild = false);
                // The buffers a simulation writes (valid after Build)
                VkBuffer GetVertexStorageBuffer() const { return vertexSSBO ? vertexSSBO->GetBuffer().Get() : VK_NULL_HANDLE; }
                VkBuffer GetBuildVertexBuffer() const { return vertexBuffers.empty() ? VK_NULL_HANDLE : vertexBuffers.back()->Get(); }
                VkBuffer GetPreviousPositionsBuffer() const { return previousPositionsSSBO ? previousPositionsSSBO->GetBuffer().Get() : VK_NULL_HANDLE; }
                // The surface's object (for its name; it cannot be moved)
                size_t GetSurfaceObject(size_t surface) const { return editableSurfaces[surface].object; }

                // Replaces a material in the scene's material buffer, uploaded with the next RecordFrameUpdate
                void SetMaterial(uint32_t index, const Fox::Graphics::Vulkan::Material& material);

                // Replaces texels of a texture (bindless index; 8 bit RGBA, tightly packed rows), uploaded with the
                // next RecordFrameUpdate
                void UpdateTextureRegion(int texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height, std::vector<uint8_t> rgba);

                // Room reserved for dynamic meshes, and how much of it is in use
                static constexpr uint32_t DYNAMIC_VERTEX_CAPACITY = 2'500'000;
                static constexpr uint32_t DYNAMIC_INDEX_CAPACITY = 24'000'000; // Cluster levels: about twice a mesh's triangles
                static constexpr uint32_t MAX_SUBMESHES = 131'072;              // One per cluster of a mesh traced through clusters
                static constexpr uint32_t MAX_MATERIALS = 4'096;
                static constexpr uint32_t MAX_INSTANCES = 131'072; // Painted forests: one per tree; clutter: one per copy
                static constexpr uint32_t MAX_CLUTTER_INSTANCES = 98'304; // Of them, at most this many for instanced objects
                uint32_t GetDynamicVertexCount() const { return static_cast<uint32_t>(vertices.size()) - staticCounts.vertices; }
                uint32_t GetDynamicIndexCount() const { return static_cast<uint32_t>(indices.size()) - staticCounts.indices; }


                size_t GetObjectCount() const { return placements.size(); }
                const std::string& GetObjectName(size_t object) const { return placements[object].name; }
                bool IsObjectMovable(size_t object) const { return placements[object].movable; }
                const ObjectTransform& GetObjectTransform(size_t object) const { return placements[object].transform; }
                // Moves what is traced, not the object: applied after its transform (a tree swaying in the wind)
                // Its instances' visibility mask instead of their surface kind's (INSTANCE_MASK_EFFECT: casts no shadow). Before Build.
                void SetObjectMask(size_t object, uint32_t mask) { placements[object].maskOverride = mask; }
                // Generated geometry with one material as a movable object (text); returns the object. Before Build.
                size_t AddGeneratedObject(const std::vector<Fox::Graphics::Vulkan::Vertex>& surfaceVertices, const std::vector<uint32_t>& surfaceIndices,
                    const Fox::Graphics::Vulkan::Material& material, const glm::mat4& transform, const std::string& name);
                void SetObjectSway(size_t object, const glm::mat4& sway) {
                    placements[object].sway = sway;
                    tlasDirty = true;
                }

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
                // mark: optional GPU profiler marks between its steps
                void RecordFrameUpdate(VkCommandBuffer cmd, uint32_t frameSlot, const std::function<void(const char*)>& mark = {});

                // Per acceleration structure instance: world space now -> world space in the previous frame
                struct InstanceMotion {
                    glm::vec4 rows[3]; // 3x4 matrix, row major
                    glm::ivec4 lights{ 0 }; // x: its object's first light - its mesh's first object's (light indices of hits);
                                            // y: 1 = its skinned mesh held its shape this frame (no deformation motion)
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

                // What the acceleration structures hold (the GPU Profiler window)
                struct GeometryStats {
                    uint64_t tracedTriangles = 0;   // In the visible TLAS instances: what rays can hit this frame
                    uint64_t clusterTriangles = 0;  // Of them, in the trees' clusters (estimated: selected clusters x their average size)
                    uint64_t storedTriangles = 0;   // In the bottom level structures, each once (instances share them)
                    uint64_t blasCount = 0;
                    uint64_t blasBytes = 0;
                    uint64_t tlasInstances = 0;
                    uint64_t visibleInstances = 0;
                    uint64_t tlasBytes = 0;
                    uint64_t objects = 0;           // Placements of meshes
                    uint64_t vertices = 0;
                    uint64_t materials = 0;
                    uint64_t lights = 0;
                    uint64_t clusterInstances = 0;
                    uint64_t clustersSelected = 0;
                    uint64_t clusterBytes = 0;
                    uint64_t geometryBytes = 0;     // Vertex and index buffers
                };
                GeometryStats GetGeometryStats() const;

                // Instance visibility bits, must match the masks in Shaders/common.slang
                static constexpr uint32_t INSTANCE_MASK_OPAQUE = 0x01;
                static constexpr uint32_t INSTANCE_MASK_GLASS = 0x02;       // Thin glass (windows)
                static constexpr uint32_t INSTANCE_MASK_SOLID_GLASS = 0x04; // Refracting solids
                static constexpr uint32_t INSTANCE_MASK_EFFECT = 0x08;      // Glowing particles: no shadows
                static constexpr uint32_t INSTANCE_MASK_UI = 0x10;          // On the screen (3D UI text): rays from the camera only

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

                // --- The hybrid path tracer (Graphics/Vulkan/Raytracing/HybridRasterPass) ---
                // Clusters of up to 128 triangles of every bottom level structure, with bounding spheres, and per top
                // level instance its clusters; the trees' level of detail clusters after them
                Fox::Graphics::Vulkan::RayTracing::HybridRasterPass::Content MakeRasterContent() const;
                // Changes when the instances or structures do (the content is then made again)
                uint32_t GetRasterVersion() const { return rasterVersion; }
                // This frame's top level instances (as the last RecordFrameUpdate wrote them; written: this frame)
                VkBuffer GetFrameInstances(bool& writtenThisFrame) const {
                    writtenThisFrame = instancesWrittenThisFrame;
                    return lastInstanceSlot < frameInstanceBuffers.size() && frameInstanceBuffers[lastInstanceSlot] ? frameInstanceBuffers[lastInstanceSlot]->Get() : VK_NULL_HANDLE;
                }
                uint32_t GetInstanceCount() const { return static_cast<uint32_t>(instances.size()); }
                // The trees' cluster level of detail this frame (false: none)
                bool GetClusterSelection(uint32_t frameSlot, VkBuffer& clusterInstances, VkBuffer& counts, VkBuffer& selected, uint32_t& count,
                    VkBuffer& graph) const {
                    return clusterPass && !clusterMeshes.empty() && clusterPass->GetSelection(frameSlot, clusterInstances, counts, selected, count, graph);
                }
                // The hybrid raster's own cut through the trees' clusters is coarser by this much (display pixels per
                // raster pixel; 0: none): each tree's candidates then cover its levels too
                void SetRasterLodScale(float scale) { rasterLodScale = scale; }
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
                // Per bottom level structure: its submeshes (the hybrid raster's clusters)
                struct BlasRange { uint32_t firstSubmesh = 0, submeshCount = 0; bool dynamic = false; };
                std::vector<BlasRange> blasRanges;
                uint32_t rasterVersion = 1;
                float rasterLodScale = 0.0f;
                uint32_t lastInstanceSlot = 0;
                bool instancesWrittenThisFrame = false;
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
                    float leafFront = 1e30f, leafSpan = 1.0f;
                    std::vector<JointMatrix> joints;
                    SkinUpdate update = SkinUpdate::Deform;
                    bool settled = false; // Frozen and caught up: nothing to do
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

                // Meshes traced through clusters (dynamic only): their clusters go to the cluster pass
                struct ClusterMesh {
                    uint32_t firstCluster = 0; // In clusterContent
                    uint32_t clusterCount = 0;
                    std::vector<Fox::Graphics::Geometry::ClusterLod::Level> levels; // firstCluster: global
                    glm::vec4 bounds{ 0.0f }; // Around every cluster's bounds (mesh space)
                };
                std::vector<ClusterMesh> clusterMeshes;
                Fox::Graphics::Vulkan::RayTracing::ClusterLodPass::Content clusterContent;
                std::unique_ptr<Fox::Graphics::Vulkan::RayTracing::ClusterLodPass> clusterPass;
                Fox::Graphics::Vulkan::RayTracing::ClusterLodPass::View clusterView;
                float clusterThreshold = 1.0f;
                float clusterOverload = 1.0f; // Threshold multiplier while the selection nears the budget
                static bool clusterLodEnabled;
                static constexpr uint32_t NO_BLAS = UINT32_MAX; // Instances whose structure the cluster pass makes each frame
                void RecordClusterLod(VkCommandBuffer cmd, uint32_t frameSlot, VkBuffer tlasInstances, const std::function<void(const char*)>& mark);

                // Editable surfaces and their pending uploads (RecordEdits)
                struct EditableSurface {
                    size_t record = 0;
                    size_t object = 0;
                    uint32_t firstPrevious = 0; // Simulated: its previous positions
                    uint32_t deformations = 0;  // Simulated: refits since it was last built
                    uint32_t firstVertex = 0; // In the scene's vertex buffers
                    uint32_t vertexCount = 0;
                    uint32_t material = 0;
                    uint32_t dirtyBegin = UINT32_MAX, dirtyEnd = 0; // Vertices to upload, surface relative
                    std::vector<glm::vec4> previousUpload;          // Previous positions to upload (UpdateSurfacePrevious)
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
                    int clusterMesh = -1; // Traced through clusters (clusterMeshes): no structures of its own; one submesh per cluster

                    // Its emissive triangles, each a light (mesh space), after the mesh's own lights in each placement's.
                    // The submeshes' light indices are those of the first placement, which starts at firstLight.
                    std::vector<Fox::Graphics::Vulkan::Light> triangleLights;
                    uint32_t firstLight = 0;

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
                    glm::mat4 sway{ 1.0f }; // World space, after its transform: a tree's lean in the wind (SetObjectSway)
                    // Instanced objects: each copy's matrix (before the transform) and, per top level instance, its copy
                    std::vector<glm::mat4> items;
                    std::vector<uint32_t> instanceItems;
                    uint32_t maskOverride = 0; // Non-zero: its instances' mask (SetObjectMask)

                    glm::mat4 TracedMatrix() const { return sway * transform.Matrix(); }
                };
                std::vector<Placement> placements;
                std::vector<uint32_t> instancePlacements; // Per TLAS instance

                static Fox::Graphics::Vulkan::Light TransformLight(Fox::Graphics::Vulkan::Light light, const glm::mat4& transform);

                // Emissive surfaces: a light per triangle of the emissive submeshes [first, first + count), whose light
                // indices then start at firstLightIndex (submesh pad2 = 1 + that)
                std::vector<Fox::Graphics::Vulkan::Light> MakeTriangleLights(uint32_t first, uint32_t count, uint32_t firstLightIndex);
                static constexpr size_t MAX_TRIANGLE_LIGHTS = 1u << 16; // Per mesh
                // Each light's chance of being sampled: the sun and lamps evenly, the emissive quads and triangles by
                // the power they give off, half and half when there are both
                void BuildLightSelection();

                bool tlasDirty = false;
                bool lightsDirty = false;
                uint32_t clutterInstances = 0; // Top level instances taken by instanced objects
                size_t AddInstancedPlacement(size_t record, std::vector<glm::mat4> items, const std::string& name);
                std::vector<InstanceMotion> uploadedMotion; // As last written to instanceMotionSSBO (only changes are uploaded)
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
