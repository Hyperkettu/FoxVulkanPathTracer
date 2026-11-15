public struct Vertex
{
    public float4 position : SV_Position;
    public float2 uv       : SV_TexCoord;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void main(
    out OutputIndices<uint3, 2> triangles,
    out OutputVertices<Vertex, 4> vertices)
{
    // Reserve 4 vertices (quad corners) and 2 triangles
    SetMeshOutputCounts(4, 2);

    // Quad positions in Clip Space [-1, 1]
    const float2 pos[4] = {
        float2(-1.0, -1.0), // 0: Top-Left
        float2( 1.0, -1.0), // 1: Top-Right
        float2(-1.0,  1.0), // 2: Bottom-Left
        float2( 1.0,  1.0)  // 3: Bottom-Right
    };

    // Corresponding UV coordinates [0, 1]
    const float2 uvs[4] = {
        float2(0.0, 1.0),   // Top-Left
        float2(1.0, 1.0),   // Top-Right
        float2(0.0, 0.0),   // Bottom-Left
        float2(1.0, 0.0)    // Bottom-Right
    };

    // Emit 4 Vertices
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        VertexOutput v;
        v.position = float4(pos[i], 0.0, 1.0);
        v.uv       = uvs[i];
        vertices[i] = v;
    }

    // Emit 2 Triangles forming the quad
    triangles[0] = uint3(0, 1, 2); // First triangle (Top-Left -> Top-Right -> Bottom-Left)
    triangles[1] = uint(2, 1, 3); // Second triangle (Bottom-Left -> Top-Right -> Bottom-Right)
}