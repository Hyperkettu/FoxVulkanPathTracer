// Cluster level of detail, every frame (see Graphics/Vulkan/Raytracing/ClusterLodPass and Graphics/Geometry/ClusterLod.h).
// Compiled once per pass (PASS):
//
//   0 select    per (instance, candidate cluster): is the cluster in the instance's cut through the graph?
//               Collects the cluster in the instance's list and marks it used this frame.
//   1 prepare   a workgroup per cluster: a used cluster gets a slot in this frame's cluster build, its instantiation info (its
//               template and its vertices), and its vertices gathered from the skinned vertex buffer (where the wind put them)
//   2 refs      per (instance, selected cluster): the address of the cluster's acceleration structure, and per
//               instance the bottom level build info listing them
//   3 patch     per instance: the address of its new bottom level structure, into its top level instance

// Mirrors Fox::Graphics::Vulkan::Vertex (Vertex.h)
struct Vertex
{
    float4 position;
    float4 normal;
    float2 uv;
    float2 padding;
    float4 tangent;
};

// Mirrors ClusterLodPass::GpuCluster
struct Cluster
{
    float4 selfBounds;    // Centre, radius in mesh space
    float4 parentBounds;
    float selfError;      // Mesh units
    float parentError;    // >= 3e38: none (the coarsest)
    uint gatherFirst;     // Its vertices: gLocalVertexIds / gPositions entries [first, first + count)
    uint gatherCount;
};

// Mirrors ClusterLodPass::Instance
struct Instance
{
    float4 rows[3];        // Mesh to world, 3x4 row major
    float scale;           // Largest axis scale
    uint firstCandidate;   // Global cluster index of its first candidate
    uint candidateCount;   // Candidates: the clusters of the levels its distance can use
    uint refBase;          // Its range of gSelected / gRefs
    uint workBase;         // Its first thread of the select and refs passes
    uint tlasInstance;     // Its top level instance
    float threshold;       // Pixels of error allowed
    uint pad;
};

struct Constants
{
    float3 cameraPosition;
    float pixelScale;      // Pixels per world unit at distance 1: image height / (2 tan(vertical fov / 2))
    uint frameStamp;
    uint workCount;
    uint clusterCount;
    uint instanceCount;
    uint refsAddressLow;   // Device address of gRefs
    uint refsAddressHigh;
    uint bottomLevelBudget; // The most clusters the instance structures may hold in all
    uint pad1;
};

[[vk::push_constant]] ConstantBuffer<Constants> constants;

[[vk::binding(0, 0)]] StructuredBuffer<Instance> gInstances;
[[vk::binding(1, 0)]] StructuredBuffer<Cluster> gClusters;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> gInstanceCounts;  // Clusters selected per instance
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> gSelected;        // Per instance range: global cluster indices
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> gUsedStamp;       // Per cluster: the last frame it was selected
[[vk::binding(5, 0)]] RWStructuredBuffer<uint> gClusterSlot;     // Per cluster: its slot in this frame's build
[[vk::binding(6, 0)]] RWStructuredBuffer<uint> gCounters;        // 0: clusters built, 1: instances, 2: clusters selected, 3: in structures
[[vk::binding(7, 0)]] StructuredBuffer<uint4> gBuildTemplates;   // Per cluster: its 32 byte instantiation info
[[vk::binding(8, 0)]] RWStructuredBuffer<uint4> gBuildInfos;     // Per slot: this frame's instantiation infos
[[vk::binding(9, 0)]] StructuredBuffer<uint> gLocalVertexIds;    // Scene vertex index of each cluster vertex
[[vk::binding(10, 0)]] RWStructuredBuffer<float> gPositions;     // 3 floats per cluster vertex: the build's input
[[vk::binding(11, 0)]] StructuredBuffer<Vertex> gVertices;       // The scene's (skinned) vertices
[[vk::binding(12, 0)]] StructuredBuffer<uint2> gClusterAddresses; // Per slot, from the cluster build
[[vk::binding(13, 0)]] RWStructuredBuffer<uint2> gRefs;          // Per instance range: cluster structure addresses
[[vk::binding(14, 0)]] RWStructuredBuffer<uint4> gBottomLevelInfos; // Per instance: count, stride, address of its refs
[[vk::binding(15, 0)]] StructuredBuffer<uint2> gBottomLevelAddresses; // Per instance, from the bottom level build
[[vk::binding(16, 0)]] RWByteAddressBuffer gTlasInstances;       // VkAccelerationStructureInstanceKHR, 64 bytes each

static const float NO_PARENT = 3.0e38f;
static const uint PREPARE_ROW = 32768; // Workgroups per row of the prepare pass (ClusterLodPass::PREPARE_ROW)

// The instance that thread t works for: the last one whose first thread is <= t
uint InstanceOfWork(uint t)
{
    uint low = 0;
    uint high = constants.instanceCount - 1;
    while (low < high)
    {
        uint middle = (low + high + 1) / 2;
        if (gInstances[middle].workBase <= t)
            low = middle;
        else
            high = middle - 1;
    }
    return low;
}

// How many pixels an error of this size (mesh units) at these bounds covers on screen
float ProjectedError(float error, float4 bounds, Instance instance)
{
    if (error >= NO_PARENT)
        return NO_PARENT;
    float3x4 toWorld = float3x4(instance.rows[0], instance.rows[1], instance.rows[2]);
    float3 center = mul(toWorld, float4(bounds.xyz, 1.0f));
    float distance = max(length(center - constants.cameraPosition) - bounds.w * instance.scale, 0.01f);
    return error * instance.scale * constants.pixelScale / distance;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 lane : SV_GroupThreadID)
{
#if PASS == 0
    // --- Select ---
    uint t = id.x;
    if (t >= constants.workCount)
        return;
    uint i = InstanceOfWork(t);
    Instance instance = gInstances[i];
    uint k = t - instance.workBase;
    if (k >= instance.candidateCount)
        return;
    uint c = instance.firstCandidate + k;
    Cluster cluster = gClusters[c];

    // In the cut: fine enough itself, while the group that would replace it is not
    if (ProjectedError(cluster.selfError, cluster.selfBounds, instance) <= instance.threshold
        && ProjectedError(cluster.parentError, cluster.parentBounds, instance) > instance.threshold)
    {
        uint slot;
        InterlockedAdd(gInstanceCounts[i], 1, slot);
        gSelected[instance.refBase + slot] = c;
        gUsedStamp[c] = constants.frameStamp;
        uint ignored;
        InterlockedAdd(gCounters[2], 1, ignored);
    }

#elif PASS == 1
    // --- Prepare --- (a workgroup per cluster, in rows of PREPARE_ROW groups: its threads gather its vertices
    // side by side; one thread looping over a cluster's hundred-odd vertices left the GPU mostly waiting)
    uint c = group.y * PREPARE_ROW + group.x;
    if (c >= constants.clusterCount || gUsedStamp[c] != constants.frameStamp)
        return;
    if (lane.x == 0)
    {
        uint slot;
        InterlockedAdd(gCounters[0], 1, slot);
        gClusterSlot[c] = slot;
        for (uint q = 0; q < 2; q++)
            gBuildInfos[slot * 2 + q] = gBuildTemplates[c * 2 + q];
    }

    Cluster cluster = gClusters[c];
    for (uint v = lane.x; v < cluster.gatherCount; v += 64)
    {
        uint local = cluster.gatherFirst + v;
        float3 p = gVertices[gLocalVertexIds[local]].position.xyz;
        gPositions[local * 3 + 0] = p.x;
        gPositions[local * 3 + 1] = p.y;
        gPositions[local * 3 + 2] = p.z;
    }

#elif PASS == 2
    // --- Refs ---
    uint t = id.x;
    if (t >= constants.workCount)
        return;
    uint i = InstanceOfWork(t);
    Instance instance = gInstances[i];
    uint k = t - instance.workBase;
    uint count = gInstanceCounts[i];
    if (k == 0)
    {
        // Its room in the instance structures; past the budget it has none this frame (the scene makes every
        // instance coarser when the selection nears the budget, so this is only a safeguard)
        uint used;
        InterlockedAdd(gCounters[3], count, used);
        uint listed = used + count <= constants.bottomLevelBudget ? count : 0;

        // Its bottom level structure: the clusters at gRefs[refBase ...] (64 bit address, added with carry)
        uint offset = instance.refBase * 8;
        uint low = constants.refsAddressLow + offset;
        uint high = constants.refsAddressHigh + (low < offset ? 1 : 0);
        gBottomLevelInfos[i] = uint4(listed, 8, low, high);
    }
    if (k >= count)
        return;
    uint c = gSelected[instance.refBase + k];
    gRefs[instance.refBase + k] = gClusterAddresses[gClusterSlot[c]];

#elif PASS == 3
    // --- Patch ---
    uint i = id.x;
    if (i >= constants.instanceCount)
        return;
    // accelerationStructureReference: the last 8 bytes of the 64 byte instance (none: inactive this frame)
    uint2 address = gBottomLevelInfos[i].x > 0 ? gBottomLevelAddresses[i] : uint2(0, 0);
    gTlasInstances.Store2(gInstances[i].tlasInstance * 64 + 56, address);
#endif
}
