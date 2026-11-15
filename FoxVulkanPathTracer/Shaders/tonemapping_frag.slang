// Tone mapping pass: linear HDR radiance -> display-referred LDR written to the swapchain.

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

// The path tracer's accumulation image.
[[vk::binding(0, 0)]] Sampler2D hdrBuffer;

static const float EXPOSURE = 1.0f;

// ACES filmic curve (Narkowicz fit)
float3 ACESFilm(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target
{
    float3 color = hdrBuffer.Sample(input.uv).rgb * EXPOSURE;

    color = ACESFilm(color);

    // The swapchain uses a UNORM format, so the sRGB transfer curve is applied here.
    color = pow(color, float3(1.0f / 2.2f));

    return float4(color, 1.0f);
}
