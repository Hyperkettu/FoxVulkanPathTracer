// Linear blend skinning: places the vertices of a skinned mesh from their bind pose and the joint matrices
// of this frame. Writes the scene's vertex buffers in place (the copy the closest hit shader reads, and the
// acceleration structure's input), and keeps where each vertex was in the previous frame (motion vectors).
// See SkinningPass.cpp and RayTracingScene::RecordFrameUpdate.

// Mirrors Fox::Graphics::Vulkan::Vertex (Vertex.h)
struct Vertex
{
    float4 position;
    float4 normal;
    float2 uv;
    float2 padding;
    float4 tangent;
};

// Mirrors RayTracingScene::SkinVertex
struct SkinVertex
{
    float4 position;
    float4 normal;
    float4 tangent;   // xyz + handedness
    uint4 joints;
    float4 weights;
};

// Mirrors RayTracingScene::JointMatrix: pose * inverse(bind), 3x4 row major
struct JointMatrix
{
    float4 rows[3];
};

struct SkinningConstants
{
    uint firstVertex;    // In the scene's vertex buffers
    uint vertexCount;
    uint firstSkinVertex; // In gBindPose and gPreviousPositions
    uint firstJoint;
};

[[vk::binding(0, 0)]] StructuredBuffer<SkinVertex> gBindPose;
[[vk::binding(1, 0)]] StructuredBuffer<JointMatrix> gJoints;
[[vk::binding(2, 0)]] RWStructuredBuffer<Vertex> gVertices;        // Read by the closest hit shader
[[vk::binding(3, 0)]] RWStructuredBuffer<Vertex> gBuildVertices;   // Acceleration structure input
[[vk::binding(4, 0)]] RWStructuredBuffer<float4> gPreviousPositions;

[[vk::push_constant]] ConstantBuffer<SkinningConstants> constants;

float3x4 JointTransform(uint joint)
{
    JointMatrix m = gJoints[constants.firstJoint + joint];
    return float3x4(m.rows[0], m.rows[1], m.rows[2]);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= constants.vertexCount)
        return;

    SkinVertex bind = gBindPose[constants.firstSkinVertex + id.x];

    float3x4 skin = JointTransform(bind.joints.x) * bind.weights.x
                  + JointTransform(bind.joints.y) * bind.weights.y
                  + JointTransform(bind.joints.z) * bind.weights.z
                  + JointTransform(bind.joints.w) * bind.weights.w;

    // Joints only rotate and translate (rigid bones), so normals transform like directions
    float3 position = mul(skin, float4(bind.position.xyz, 1.0f));
    float3 normal = normalize(mul(skin, float4(bind.normal.xyz, 0.0f)));
    float3 tangent = mul(skin, float4(bind.tangent.xyz, 0.0f));
    tangent = dot(tangent, tangent) > 1e-12f ? normalize(tangent) : tangent;

    uint index = constants.firstVertex + id.x;
    Vertex vertex = gVertices[index];

    // Before overwriting: this is where the vertex was in the previous frame
    gPreviousPositions[constants.firstSkinVertex + id.x] = float4(vertex.position.xyz, 1.0f);

    vertex.position = float4(position, 1.0f);
    vertex.normal = float4(normal, 0.0f);
    vertex.tangent = float4(tangent, bind.tangent.w);

    gVertices[index] = vertex;
    gBuildVertices[index] = vertex;
}
