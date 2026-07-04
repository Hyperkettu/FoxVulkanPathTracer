struct Vertex
{
    float4 position;
    float4 normal;
    float2 uv;
};

struct Submesh
{
    uint vertexOffset;   // Optional for global indices
    uint indexOffset;
    uint vertexCount;
    uint indexCount;
};

[[vk::binding(3, 0)]]
StructuredBuffer<Vertex> gAllVertices;

[[vk::binding(4, 0)]]
StructuredBuffer<uint> gAllIndices;

[[vk::binding(5, 0)]]
StructuredBuffer<Submesh> gAllSubmeshes;

struct RayPayload
{
    float3 hitColor;
};

[shader("closesthit")]
void main(
    inout RayPayload payload,
    BuiltInTriangleIntersectionAttributes attribs)
{
    // Instance corresponds directly to one Submesh
    uint submeshID = InstanceID();
    uint primitiveID = PrimitiveIndex();

    Submesh submesh = gAllSubmeshes[submeshID];

    // Triangle indices
    uint baseIndex = submesh.indexOffset + primitiveID * 3;

    uint i0 = gAllIndices[baseIndex + 0];
    uint i1 = gAllIndices[baseIndex + 1];
    uint i2 = gAllIndices[baseIndex + 2];

    // Fetch vertices
    Vertex v0 = gAllVertices[i0];
    Vertex v1 = gAllVertices[i1];
    Vertex v2 = gAllVertices[i2];

    // Barycentrics
    float3 barycentrics = float3(
        1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
        attribs.barycentrics.x,
        attribs.barycentrics.y);

    // Interpolate normal
    float3 localNormal =
        normalize(
            v0.normal.xyz * barycentrics.x +
            v1.normal.xyz * barycentrics.y +
            v2.normal.xyz * barycentrics.z);

    float3x3 normalMatrix = ((float3x3)WorldToObject3x4());

    float3 worldNormal =
        normalize(mul(localNormal, normalMatrix));

    // Visualize normal
    payload.hitColor = worldNormal * 0.5 + 0.5;

  

}