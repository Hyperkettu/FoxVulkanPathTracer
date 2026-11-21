// The hybrid path tracer's cluster culling (Graphics/Vulkan/Raytracing/HybridRasterPass), as Nanite does it: every
// cluster of every visible instance is tested against the view frustum and against a depth pyramid (HiZ), and those
// that pass become indirect draws of the visibility buffer. Two phases:
//
//   phase 1   against the previous frame's depth pyramid (where the cluster was then): what was visible is drawn at
//             once; what was hidden becomes a candidate
//   phase 2   the candidates, against the pyramid of what phase 1 drew: whatever came into view is drawn too
//
// So nothing visible is missed (phase 2 catches what moved into view) and hidden clusters are mostly never drawn.
// Compiled once per pass (PASS):
//
//   0  phase 1, per top level instance (a thread each): its clusters, when it has a few (grass, stones); instances
//      with more are listed for pass 3
//   3  phase 1, per instance with many clusters (a group each)
//   1  phase 1, per tree drawn through cluster level of detail (a group each): the clusters selected this frame
//   2  phase 2, per candidate

#include "hybrid_common.slang"

#ifndef PASS
#define PASS 0
#endif

// Mirrors Fox::Graphics::Vulkan::Submesh (only the first fields are read)
struct SubmeshRange
{
    uint vertexOffset;
    uint indexOffset;
    uint vertexCount;
    uint indexCount;
    uint materialIndex;
    uint previousPositions;
    uint pad1;
    uint pad2;
};

// Mirrors RayTracingScene::InstanceMotion: world now -> world in the previous frame
struct InstanceMotion
{
    float4 rows[3];
    int4 lights;
};

// Mirrors ClusterLodPass::GpuCluster (cluster_lod.slang)
struct LodCluster
{
    float4 selfBounds;
    float4 parentBounds;
    float selfError;
    float parentError;
    uint gatherFirst;
    uint gatherCount;
};
static const float NO_PARENT = 3.0e38f;

// Mirrors ClusterLodPass::Instance (cluster_lod.slang)
struct ClusterInstance
{
    float4 rows[3];
    float scale;
    uint firstCandidate;
    uint candidateCount;
    uint refBase;
    uint workBase;
    uint tlasInstance;
    float threshold;
    uint pad;
};

[[vk::binding(0, 0)]] ConstantBuffer<CullConstants> constants;
[[vk::binding(1, 0)]] StructuredBuffer<Chunk> gChunks;
[[vk::binding(2, 0)]] StructuredBuffer<RasterInstance> gRasterInstances;
[[vk::binding(3, 0)]] StructuredBuffer<TlasInstance> gTlas;
[[vk::binding(4, 0)]] StructuredBuffer<InstanceMotion> gMotion;
[[vk::binding(5, 0)]] StructuredBuffer<SubmeshRange> gSubmeshes;
[[vk::binding(6, 0)]] RWStructuredBuffer<uint> gDraws;           // VkDrawIndirectCommand per bucket (4 uints): instance counts added here
[[vk::binding(7, 0)]] RWStructuredBuffer<DrawInfo> gDrawInfos;   // Per draw
[[vk::binding(8, 0)]] RWStructuredBuffer<uint> gCounters;        // See COUNTER_*
[[vk::binding(9, 0)]] RWStructuredBuffer<uint2> gCandidates;     // Phase 2: (top level instance, chunk)
[[vk::binding(10, 0)]] StructuredBuffer<ClusterInstance> gClusterInstances;
[[vk::binding(11, 0)]] StructuredBuffer<uint> gClusterCounts;    // Clusters selected per tree instance
[[vk::binding(12, 0)]] StructuredBuffer<uint> gClusterSelected;  // Per tree instance range: global cluster indices
[[vk::binding(13, 0)]] Texture2D<float> gDepthPyramid;           // Farthest depth (reversed: smallest) of each texel's area
[[vk::binding(14, 0)]] StructuredBuffer<LodCluster> gLodClusters; // The trees' clusters (their level of detail graph)
[[vk::binding(15, 0)]] RWStructuredBuffer<uint> gLarge;           // Instances with many clusters (pass 3)

// gCounters: 0..3 draws per list, then
static const uint COUNTER_CANDIDATES = 4;
static const uint COUNTER_TESTED = 5;     // Clusters tested
static const uint COUNTER_INSTANCES = 6;  // Instances that passed their own test
static const uint COUNTER_TRIANGLES = 7;  // Triangles drawn
static const uint COUNTER_OCCLUDED = 8;   // Clusters found hidden (phase 1 candidates that phase 2 also rejected count again there)
static const uint COUNTER_OUTSIDE = 9;    // Clusters outside the view
static const uint COUNTER_OVERFLOW = 10;  // Draws or candidates that did not fit
static const uint COUNTER_HIDDEN = 11;    // Clusters phase 2 rejected
// 12..15: triangles per list
static const uint COUNTER_SOFTWARE = 16;  // Triangles the software rasterizer draws
static const uint COUNTER_LARGE = 17;     // Instances listed for pass 3
static const uint LARGE_CAPACITY = 16384;
static const uint FEW_CLUSTERS = 8;       // A thread tests this many itself

static const uint GROUP_SIZE = 64;

bool OutsideFrustum(float3 center, float radius)
{
    for (int p = 0; p < 6; p++)
    {
        if (dot(constants.planes[p].xyz, center) + constants.planes[p].w < -radius)
            return true;
    }
    return false;
}

// Is the sphere behind what the depth pyramid holds? The box around it is projected; the pyramid level where it
// spans at most 2x2 texels gives the farthest depth there, and the sphere is hidden when its nearest point is
// farther still (reversed depth: smaller).
bool Occluded(float4 rows[4], float3 center, float radius)
{
    float2 size = constants.hiz.xy;
    float2 lo = float2(1e9f), hi = float2(-1e9f);
    float nearest = 0.0f;
    for (uint i = 0; i < 8; i++)
    {
        float3 corner = center + radius * float3((i & 1) != 0 ? 1.0f : -1.0f, (i & 2) != 0 ? 1.0f : -1.0f, (i & 4) != 0 ? 1.0f : -1.0f);
        float4 clip = Project(rows, corner);
        if (clip.w <= 1e-3f)
            return false; // Reaches the camera: visible
        float2 pixel = (clip.xy / clip.w * 0.5f + 0.5f) * size;
        lo = min(lo, pixel);
        hi = max(hi, pixel);
        nearest = max(nearest, clip.z / clip.w);
    }
    lo = clamp(lo, float2(0.0f), size - 1.0f);
    hi = clamp(hi, float2(0.0f), size - 1.0f);
    if (any(hi < lo))
        return false;

    float span = max(hi.x - lo.x, hi.y - lo.y);
    uint levels = uint(constants.hiz.z);
    uint level = min(uint(ceil(log2(max(span, 1.0f)))), levels - 1);
    int2 a = int2(lo) >> level;
    int2 b = int2(hi) >> level;
    int2 last = max(int2(ceil(size / float(1u << level))) - 1, int2(0));
    a = min(a, last);
    b = min(b, last);
    float farthest = gDepthPyramid.Load(int3(a, level));
    farthest = min(farthest, gDepthPyramid.Load(int3(b.x, a.y, level)));
    farthest = min(farthest, gDepthPyramid.Load(int3(a.x, b.y, level)));
    farthest = min(farthest, gDepthPyramid.Load(int3(b, level)));
    return nearest < farthest;
}

// Small on screen (narrower than the software rasterizer's limit): its triangles are a pixel or so each
bool Small(float3 center, float radius)
{
    if (constants.lod.w <= 0.0f)
        return false;
    float distance = length(center - constants.cameraPosition.xyz) - radius;
    if (distance <= 4.0f * constants.cameraPosition.w)
        return false;
    return 2.0f * radius * constants.lod.z / distance < constants.lod.w;
}

void Emit(uint phase, Chunk chunk, uint tlasInstance, bool software = false)
{
    if (software)
    {
        // The software rasterizer's list: its dispatch one more group
        uint capacity = constants.limits.w;
        uint slot;
        InterlockedAdd(gDraws[SOFTWARE_DISPATCH + phase * 3], 1, slot);
        if (slot < capacity)
        {
            SubmeshRange range = gSubmeshes[chunk.submesh];
            DrawInfo record;
            record.tlasInstance = tlasInstance;
            record.submesh = chunk.submesh;
            record.firstIndex = range.indexOffset + chunk.firstTriangle * 3;
            record.triangleCount = chunk.triangleCount;
            gDrawInfos[(16 + phase) * capacity + slot] = record;
            uint list = phase * 2 + ((chunk.flags & CHUNK_MASKED) != 0 ? 1 : 0);
            InterlockedAdd(gCounters[list], 1);
            InterlockedAdd(gCounters[COUNTER_TRIANGLES], chunk.triangleCount);
            InterlockedAdd(gCounters[12 + list], chunk.triangleCount);
            InterlockedAdd(gCounters[COUNTER_SOFTWARE], chunk.triangleCount);
            return;
        }
        InterlockedAdd(gDraws[SOFTWARE_DISPATCH + phase * 3], uint(-1)); // Full: the hardware draws it
    }
    uint list = phase * 2 + ((chunk.flags & CHUNK_MASKED) != 0 ? 1 : 0);
    uint bucket = list * 4 + BucketOf(chunk.triangleCount);
    uint capacity = constants.limits.w;
    // The bucket's draw: one more instance
    uint slot;
    InterlockedAdd(gDraws[bucket * 4 + 1], 1, slot);
    if (slot >= capacity)
    {
        InterlockedAdd(gDraws[bucket * 4 + 1], uint(-1)); // Back: the draw stays within the bucket
        InterlockedAdd(gCounters[COUNTER_OVERFLOW], 1);
        return;
    }
    SubmeshRange submesh = gSubmeshes[chunk.submesh];
    DrawInfo info;
    info.tlasInstance = tlasInstance;
    info.submesh = chunk.submesh;
    info.firstIndex = submesh.indexOffset + chunk.firstTriangle * 3;
    info.triangleCount = chunk.triangleCount;
    gDrawInfos[bucket * capacity + slot] = info;
    InterlockedAdd(gCounters[list], 1);
    InterlockedAdd(gCounters[COUNTER_TRIANGLES], chunk.triangleCount);
    InterlockedAdd(gCounters[12 + list], chunk.triangleCount); // Triangles per list
}

// How many pixels an error of this size (mesh units) at these bounds covers at the raster's resolution
float ProjectedError(float error, float4 bounds, ClusterInstance instance)
{
    if (error >= NO_PARENT)
        return NO_PARENT;
    float3 center = TransformPoint(instance.rows, bounds.xyz);
    float distance = max(length(center - constants.cameraPosition.xyz) - bounds.w * instance.scale, 0.01f);
    return error * instance.scale * constants.lod.x / distance;
}

// Phase 1 for one cluster of an instance
void TestChunk(uint tlasInstance, uint chunkIndex, TlasInstance tlas, float scale)
{
    Chunk chunk = gChunks[chunkIndex];
    InterlockedAdd(gCounters[COUNTER_TESTED], 1);
    if ((chunk.flags & CHUNK_UNBOUNDED) != 0)
    {
        Emit(0, chunk, tlasInstance);
        return;
    }
    float3 center = TransformPoint(tlas.rows, chunk.sphere.xyz);
    float radius = chunk.sphere.w * scale;
    if (OutsideFrustum(center, radius))
    {
        InterlockedAdd(gCounters[COUNTER_OUTSIDE], 1);
        return;
    }
    // Hidden last frame (where it was then, against what was drawn then)? Phase 2 looks again.
    if ((constants.limits.y & 1) != 0 && constants.hiz.w > 0.5f)
    {
        float3 previous = TransformPoint(gMotion[tlasInstance].rows, center);
        if (Occluded(constants.prevViewProj, previous, radius))
        {
            uint slot;
            InterlockedAdd(gCounters[COUNTER_CANDIDATES], 1, slot);
            if (slot < constants.limits.x)
            {
                gCandidates[slot] = uint2(tlasInstance, chunkIndex);
                InterlockedAdd(gCounters[COUNTER_OCCLUDED], 1);
            }
            else
            {
                InterlockedAdd(gCounters[COUNTER_OVERFLOW], 1);
                Emit(0, chunk, tlasInstance, Small(center, radius)); // No room to look again: drawn
            }
            return;
        }
    }
    Emit(0, chunk, tlasInstance, Small(center, radius));
}

uint GroupIndex(uint3 group)
{
    return group.y * 65535 + group.x;
}

#if PASS == 0

[shader("compute")]
[numthreads(GROUP_SIZE, 1, 1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
    uint i = GroupIndex(group) * GROUP_SIZE + thread;
    if (i >= constants.counts.x)
        return;
    RasterInstance instance = gRasterInstances[i];
    if ((instance.flags & RASTER_CLUSTER_LOD) != 0 || instance.chunkCount == 0)
        return;
    TlasInstance tlas = gTlas[i];
    if ((tlas.customIndexMask >> 24) == 0)
        return; // Hidden (mask 0)
    float scale = MaxScale(tlas.rows);
    if (instance.sphere.w >= 0.0f && OutsideFrustum(TransformPoint(tlas.rows, instance.sphere.xyz), instance.sphere.w * scale))
        return;
    InterlockedAdd(gCounters[COUNTER_INSTANCES], 1);
    if (instance.chunkCount > FEW_CLUSTERS)
    {
        // Many clusters: a group of its own (pass 3)
        uint slot;
        InterlockedAdd(gCounters[COUNTER_LARGE], 1, slot);
        if (slot < LARGE_CAPACITY)
        {
            gLarge[slot] = i;
            return;
        }
    }
    for (uint c = 0; c < instance.chunkCount; c++)
        TestChunk(i, instance.firstChunk + c, tlas, scale);
}

#elif PASS == 3

[shader("compute")]
[numthreads(GROUP_SIZE, 1, 1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
    uint k = GroupIndex(group);
    if (k >= min(gCounters[COUNTER_LARGE], LARGE_CAPACITY))
        return;
    uint i = gLarge[k];
    RasterInstance instance = gRasterInstances[i];
    TlasInstance tlas = gTlas[i];
    float scale = MaxScale(tlas.rows);
    for (uint c = thread; c < instance.chunkCount; c += GROUP_SIZE)
        TestChunk(i, instance.firstChunk + c, tlas, scale);
}

#elif PASS == 1

[shader("compute")]
[numthreads(GROUP_SIZE, 1, 1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
    uint k = GroupIndex(group);
    if (k >= constants.counts.y)
        return;
    ClusterInstance cluster = gClusterInstances[k];
    uint tlasInstance = cluster.tlasInstance;
    TlasInstance tlas = gTlas[tlasInstance];
    if ((tlas.customIndexMask >> 24) == 0)
        return;
    float scale = MaxScale(tlas.rows);
    if (thread == 0)
        InterlockedAdd(gCounters[COUNTER_INSTANCES], 1);
    if (constants.lod.x > 0.0f)
    {
        // Nanite style: its own cut through the tree's cluster graph, for the pixels of this raster: a cluster is drawn
        // where it is fine enough and the group that would replace it is not. (The rays keep theirs, for the display's
        // pixels: up to a pixel's error apart.)
        float threshold = cluster.threshold * constants.lod.y;
        for (uint j = thread; j < cluster.candidateCount; j += GROUP_SIZE)
        {
            uint c = cluster.firstCandidate + j;
            LodCluster lodCluster = gLodClusters[c];
            if (ProjectedError(lodCluster.selfError, lodCluster.selfBounds, cluster) <= threshold
                && ProjectedError(lodCluster.parentError, lodCluster.parentBounds, cluster) > threshold)
                TestChunk(tlasInstance, constants.counts.z + c, tlas, scale);
        }
        return;
    }
    uint count = gClusterCounts[k];
    for (uint j = thread; j < count; j += GROUP_SIZE)
        TestChunk(tlasInstance, constants.counts.z + gClusterSelected[cluster.refBase + j], tlas, scale);
}

#else

[shader("compute")]
[numthreads(GROUP_SIZE, 1, 1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
    uint index = GroupIndex(group) * GROUP_SIZE + thread;
    if (index >= min(gCounters[COUNTER_CANDIDATES], constants.limits.x))
        return;
    uint2 candidate = gCandidates[index];
    Chunk chunk = gChunks[candidate.y];
    TlasInstance tlas = gTlas[candidate.x];
    float3 center = TransformPoint(tlas.rows, chunk.sphere.xyz);
    float radius = chunk.sphere.w * MaxScale(tlas.rows);
    if (Occluded(constants.viewProj, center, radius))
    {
        InterlockedAdd(gCounters[COUNTER_HIDDEN], 1);
        return;
    }
    Emit(1, chunk, candidate.x, (chunk.flags & CHUNK_UNBOUNDED) == 0 && Small(center, radius));
}

#endif
