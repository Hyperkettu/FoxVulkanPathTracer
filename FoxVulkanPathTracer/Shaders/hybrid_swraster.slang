// The hybrid path tracer's software rasterizer, Nanite style: clusters whose triangles are only a pixel or so on
// screen are rasterized here, in a compute shader, instead of by the hardware (whose fixed setup per triangle, and
// shading of whole 2x2 pixel quads, are wasted on triangles smaller than a pixel). A group per cluster, a thread per
// triangle: each covers the pixel centres in its bounding box with its edge functions and keeps the nearest with a
// 64-bit atomic max: reversed depth in the high 32 bits (nearer is larger), the cluster's record and the triangle in
// the low 32. The resolve (hybrid_resolve.slang) then merges these with the hardware's visibility buffer.

#include "common.slang"
#include "hybrid_common.slang"

[[vk::binding(20, 0)]] StructuredBuffer<DrawInfo> gDrawInfos;
[[vk::binding(21, 0)]] StructuredBuffer<TlasInstance> gTlas;
[[vk::binding(22, 0)]] RWStructuredBuffer<uint64_t> gSoftware;   // Per pixel: depth << 32 | record << 8 | triangle
[[vk::binding(23, 0)]] StructuredBuffer<uint> gDraws;            // The software dispatches' cluster counts

struct Constants
{
    float4 viewProj[4];
    float2 size;          // The visibility buffer's, pixels
    uint firstRecord;     // This phase's software clusters: records [firstRecord, + count)
    uint countOffset;     // Where in gDraws its count is (uints)
};
[[vk::push_constant]] ConstantBuffer<Constants> constants;

static const uint THREADS = 128;
static const int MAX_SPAN = 32; // Pixels a triangle's box may span each way here (a cluster this small has none larger)

float2 ToScreen(float4 clip)
{
    return (clip.xy / clip.w * 0.5f + 0.5f) * constants.size;
}

[shader("compute")]
[numthreads(THREADS, 1, 1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
    uint cluster = group.y * 65535 + group.x;
    if (cluster >= gDraws[constants.countOffset])
        return;
    uint record = constants.firstRecord + cluster;
    DrawInfo info = gDrawInfos[record];
    float4 rows[3] = gTlas[info.tlasInstance].rows;
    Submesh submesh = gAllSubmeshes[info.submesh];
    Material material = gAllMaterials[submesh.materialIndex];
    bool masked = material.alphaCutoff > 0.0f;

    for (uint t = thread; t < info.triangleCount; t += THREADS)
    {
        uint base = info.firstIndex + t * 3;
        Vertex v0 = gAllVertices[gAllIndices[base + 0]];
        Vertex v1 = gAllVertices[gAllIndices[base + 1]];
        Vertex v2 = gAllVertices[gAllIndices[base + 2]];
        float4 c0 = Project(constants.viewProj, TransformPoint(rows, v0.position.xyz));
        float4 c1 = Project(constants.viewProj, TransformPoint(rows, v1.position.xyz));
        float4 c2 = Project(constants.viewProj, TransformPoint(rows, v2.position.xyz));
        if (c0.w <= 1e-4f || c1.w <= 1e-4f || c2.w <= 1e-4f)
            continue; // Reaches behind the camera (a cluster this small and near: not here)
        float2 p0 = ToScreen(c0), p1 = ToScreen(c1), p2 = ToScreen(c2);
        float area = (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
        if (abs(area) < 1e-12f)
            continue;

        // The pixel centres in its box
        int2 low = int2(ceil(min(p0, min(p1, p2)) - 0.5f));
        int2 high = int2(floor(max(p0, max(p1, p2)) - 0.5f));
        low = max(low, int2(0));
        high = min(high, int2(constants.size) - 1);
        if (any(high < low) || any(high - low > MAX_SPAN))
            continue;

        float inverseArea = 1.0f / area;
        float3 z = float3(c0.z / c0.w, c1.z / c1.w, c2.z / c2.w);    // Reversed depth: linear across the screen
        float3 w = float3(1.0f / c0.w, 1.0f / c1.w, 1.0f / c2.w);    // For perspective correct uv
        uint payload = ((record & 0xFFFFFF) << 8) | (t & 0xFF);

        for (int y = low.y; y <= high.y; y++)
        {
            for (int x = low.x; x <= high.x; x++)
            {
                float2 p = float2(x, y) + 0.5f;
                // Barycentrics from the edge functions (either winding: double sided)
                float b0 = ((p1.x - p.x) * (p2.y - p.y) - (p2.x - p.x) * (p1.y - p.y)) * inverseArea;
                float b1 = ((p2.x - p.x) * (p0.y - p.y) - (p0.x - p.x) * (p2.y - p.y)) * inverseArea;
                float b2 = 1.0f - b0 - b1;
                if (b0 < 0.0f || b1 < 0.0f || b2 < 0.0f)
                    continue;
                float depth = b0 * z.x + b1 * z.y + b2 * z.z;
                if (depth <= 0.0f)
                    continue;
                if (masked)
                {
                    float3 perspective = float3(b0 * w.x, b1 * w.y, b2 * w.z);
                    perspective /= perspective.x + perspective.y + perspective.z;
                    float2 uv = v0.uv * perspective.x + v1.uv * perspective.y + v2.uv * perspective.z;
                    float alpha = material.baseColor.a;
                    if (material.baseColorTexture >= 0)
                        alpha *= SampleTexture(material.baseColorTexture, TransformUV(uv, material)).a;
                    if (alpha < material.alphaCutoff)
                        continue;
                }
                uint64_t value = (uint64_t(asuint(depth)) << 32) | uint64_t(payload);
                InterlockedMax(gSoftware[y * uint(constants.size.x) + x], value);
            }
        }
    }
}
