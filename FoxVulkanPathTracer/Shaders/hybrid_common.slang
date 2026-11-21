// Shared by the hybrid path tracer's raster passes (Graphics/Vulkan/Raytracing/HybridRasterPass.h): cluster culling
// (hybrid_cull.slang), the visibility buffer (hybrid_raster.slang), the depth pyramid (hybrid_hiz.slang), and the
// path tracer reading their results (raygen.slang).

#ifndef FOX_HYBRID_COMMON_SLANG
#define FOX_HYBRID_COMMON_SLANG

// A top level instance as Vulkan stores it (VkAccelerationStructureInstanceKHR, 64 bytes)
struct TlasInstance
{
    float4 rows[3];         // Object to world, 3x4 row major
    uint customIndexMask;   // Custom index (its first submesh) in the low 24 bits, visibility mask in the high 8
    uint sbtOffsetFlags;
    uint2 accelerationStructure;
};

// A cluster: up to 128 triangles of one submesh, with a sphere around them in the mesh's space.
// Mirrors HybridRasterPass::Chunk.
struct Chunk
{
    float4 sphere;          // Centre, radius (object space)
    uint submesh;           // Scene-wide submesh index
    uint firstTriangle;     // In the submesh
    uint triangleCount;
    uint flags;             // CHUNK_*
};
static const uint CHUNK_MASKED = 1;     // Alpha tested: drawn with the pipeline that tests alpha
static const uint CHUNK_UNBOUNDED = 2;  // Its vertices move (simulated, sculpted): no culling of its own

// Per top level instance: its clusters. Mirrors HybridRasterPass::RasterInstance.
struct RasterInstance
{
    float4 sphere;          // Around all its clusters (object space); radius < 0: unbounded
    uint firstChunk;
    uint chunkCount;
    uint flags;             // RASTER_*
    uint pad;
};
static const uint RASTER_CLUSTER_LOD = 1; // A tree drawn with the clusters its level of detail selected this frame

// A visible cluster, as drawn: the visibility buffer stores 1 + its index. Mirrors HybridRasterPass::DrawInfo.
struct DrawInfo
{
    uint tlasInstance;
    uint submesh;
    uint firstIndex;      // In the scene's index buffer (scene-wide)
    uint triangleCount;
};

// Visible clusters are not drawn one draw each: they go into buckets by size, and each bucket is one instanced draw
// (an instance per cluster, as many vertices as the bucket's largest cluster has corners; the vertex shader folds
// away the triangles a smaller cluster does not have). So a frame is 16 draws, not hundreds of thousands.
static const uint BUCKET_SIZES[4] = { 32, 64, 128, 256 }; // Triangles at most
uint BucketOf(uint triangles)
{
    return triangles <= 32 ? 0 : triangles <= 64 ? 1 : triangles <= 128 ? 2 : 3;
}

// Mirrors HybridRasterPass::CullConstants
struct CullConstants
{
    float4 viewProj[4];     // Clip = (dot(row 0, p), ...), jittered as the path tracer's rays; reversed depth (1 near, 0 far)
    float4 prevViewProj[4]; // The previous frame's
    float4 planes[6];       // World space frustum: dot(plane.xyz, p) + plane.w >= 0 inside
    float4 cameraPosition;  // w: near plane
    float4 hiz;             // Depth pyramid: width, height, levels, 1 = the previous frame's is valid
    uint4 counts;           // TLAS instances, cluster LOD instances, first chunk of the LOD clusters, draws a list holds
    uint4 limits;           // Candidates held, flags (1: occlusion culling), triangles a chunk holds at most, clusters a bucket holds
    float4 lod;             // Trees' raster level of detail: pixels per world unit at distance 1 at the raster's resolution
                            // (0: the clusters the rays use), the threshold's multiplier; that pixel scale again (always), and
                            // the software rasterizer's limit: clusters narrower than this many pixels go there (0: none do)
};

static const uint LIST_COUNT = 4; // Phase 1 opaque, phase 1 masked, phase 2 opaque, phase 2 masked
static const uint SOFTWARE_DISPATCH = 64; // In the draw buffer (uints): after the 16 draws, a dispatch (x, y, z) per phase

float4 Project(float4 rows[4], float3 p)
{
    float4 h = float4(p, 1.0f);
    return float4(dot(rows[0], h), dot(rows[1], h), dot(rows[2], h), dot(rows[3], h));
}

float3 TransformPoint(float4 rows[3], float3 p)
{
    float4 h = float4(p, 1.0f);
    return float3(dot(rows[0], h), dot(rows[1], h), dot(rows[2], h));
}

float MaxScale(float4 rows[3])
{
    float3 x = float3(rows[0].x, rows[1].x, rows[2].x);
    float3 y = float3(rows[0].y, rows[1].y, rows[2].y);
    float3 z = float3(rows[0].z, rows[1].z, rows[2].z);
    return sqrt(max(dot(x, x), max(dot(y, y), dot(z, z))));
}

#endif
