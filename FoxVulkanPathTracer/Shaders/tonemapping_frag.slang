// ToneMapFragment.slang

struct Vertex
{
    float4 position : SV_Position;
    float2 uv       : SV_TexCoord;
};

[[vk::binding(0, 0)]]
Texture2D<half4> hdrBuffer; 

[[vk::binding(1, 0)]]
SamplerState sampler;

/*cbuffer ToneMapParams : register(b0)
{
    float exposure;
}; */

// ACES Narkowicz / Fitted Curve for HDR -> LDR mapping
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target
{
    float exposure = 1.0f;
    half4 hdrColor = hdrBuffer.Sample(sampler, input.uv);

    // 2. Exposure scale
    float3 color = hdrColor.rgb * exposure;

    color = ACESFilm(color);

    color = pow(color, float3(1.0 / 2.2));

    return float4(color, hdrColor.a);
}