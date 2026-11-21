// The hybrid path tracer's visibility buffer, vertex stage (Graphics/Vulkan/Raytracing/HybridRasterPass): each
// bucket of visible clusters is one instanced draw, an instance per cluster (gl_InstanceIndex: the cluster's record),
// as many vertices as the bucket's largest cluster has corners. Each vertex reads its index and vertex from the scene's
// buffers (the very data the rays hit), placed by its top level instance's transform; corners past the cluster's
// triangles fold into a point (no triangle).

#include "common.slang"
#include "hybrid_common.slang"

[[vk::binding(20, 0)]] StructuredBuffer<DrawInfo> gDrawInfos;
[[vk::binding(21, 0)]] StructuredBuffer<TlasInstance> gTlas;

struct Constants
{
    float4 viewProj[4];
};
[[vk::push_constant]] ConstantBuffer<Constants> constants;

struct VertexOutput
{
    float4 position : SV_Position;
    nointerpolation uint draw : DRAW;
    nointerpolation uint triangle : TRIANGLE;
    nointerpolation uint material : MATERIAL;
    float2 uv : UV;
};

[shader("vertex")]
VertexOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID, uint baseInstance : SV_StartInstanceLocation)
{
    uint record = baseInstance + instanceId;
    DrawInfo info = gDrawInfos[record];
    VertexOutput output;
    output.draw = record;
    output.material = 0;
    output.uv = float2(0.0f);
    output.triangle = 0;
    if (vertexId / 3 >= info.triangleCount)
    {
        output.position = float4(0.0f, 0.0f, 0.0f, 1.0f); // Not a triangle of this cluster: a point
        return output;
    }
    uint index = info.firstIndex + vertexId;
    Vertex vertex = gAllVertices[gAllIndices[index]];
    float3 world = TransformPoint(gTlas[info.tlasInstance].rows, vertex.position.xyz);

    output.position = Project(constants.viewProj, world);
    output.triangle = index / 3; // Scene-wide triangle (submeshes start at multiples of 3 indices)
    output.material = gAllSubmeshes[info.submesh].materialIndex;
    output.uv = vertex.uv;
    return output;
}
