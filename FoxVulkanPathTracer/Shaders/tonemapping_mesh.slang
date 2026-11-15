// Emits a single full-screen quad for the tone mapping pass. No vertex buffers involved.

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void main(
    out indices uint3 triangles[2],
    out vertices VertexOutput vertices[4])
{
    SetMeshOutputCounts(4, 2);

    // Vulkan clip space: (-1, -1) is the top-left corner, which is also texel (0, 0).
    const float2 positions[4] = {
        float2(-1.0, -1.0), // 0: Top-Left
        float2( 1.0, -1.0), // 1: Top-Right
        float2(-1.0,  1.0), // 2: Bottom-Left
        float2( 1.0,  1.0)  // 3: Bottom-Right
    };

    for (int i = 0; i < 4; ++i)
    {
        VertexOutput v;
        v.position = float4(positions[i], 0.0, 1.0);
        v.uv       = positions[i] * 0.5 + 0.5;
        vertices[i] = v;
    }

    triangles[0] = uint3(0, 1, 2);
    triangles[1] = uint3(2, 1, 3);
}
