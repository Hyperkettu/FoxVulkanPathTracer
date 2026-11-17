// 2D overlay (editor gizmos): triangles given in pixels, see OverlayRenderer.cpp

struct OverlayConstants
{
    float2 viewportSize;
};

[[vk::push_constant]] ConstantBuffer<OverlayConstants> constants;

struct VertexInput
{
    [[vk::location(0)]] float2 position : POSITION; // Pixels, (0, 0) at the top left
    [[vk::location(1)]] float4 color : COLOR;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
    VertexOutput output;
    // Vulkan clip space has y pointing down, like pixels
    output.position = float4(input.position / constants.viewportSize * 2.0f - 1.0f, 0.0f, 1.0f);
    output.color = input.color;
    return output;
}
