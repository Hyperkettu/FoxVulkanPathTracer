// Tone mapping pass: linear HDR radiance -> display-referred LDR written to the swapchain.

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

// The denoised output of the path tracer
[[vk::binding(0, 0)]] Sampler2D hdrBuffer;

// Mirrors ToneMappingConstants in PostProcess.cpp
struct ToneMappingConstants
{
    float exposure; // Linear scale applied before the tone curve
};

[[vk::push_constant]] ToneMappingConstants constants;

// ------------------------------------------------------------------------------------------------
// AgX (Troy Sobotka), using the compact fit by Benjamin Wrensch.
// Compared to ACES it keeps hues stable and lets very bright colours roll off towards white
// instead of clipping to saturated primaries, which suits path traced skies and light sources.
// ------------------------------------------------------------------------------------------------

static const float AGX_MIN_EV = -12.47393f;
static const float AGX_MAX_EV = 4.026069f;

float3 AgXContrastCurve(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;

    return 15.5f * x4 * x2
         - 40.14f * x4 * x
         + 31.96f * x4
         - 6.868f * x2 * x
         + 0.4298f * x2
         + 0.1191f * x
         - 0.00232f;
}

// Linear sRGB -> AgX log encoding with the base contrast curve applied
float3 AgX(float3 color)
{
    const float3x3 inset = float3x3(
        0.842479062253094f,  0.0784335999999992f, 0.0792237451477643f,
        0.0423282422610123f, 0.878468636469772f,  0.0791661274605434f,
        0.0423756549057051f, 0.0784336f,          0.879142973793104f);

    color = mul(inset, color);
    color = clamp(log2(max(color, 1e-10f)), AGX_MIN_EV, AGX_MAX_EV);
    color = (color - AGX_MIN_EV) / (AGX_MAX_EV - AGX_MIN_EV);

    return AgXContrastCurve(color);
}

// Creative grade on the AgX encoded image: a touch more contrast and saturation than the neutral base
float3 AgXLook(float3 color)
{
    const float power = 1.25f;
    const float saturation = 1.35f;

    float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    color = pow(max(color, 0.0f), power);
    return luma + saturation * (color - luma);
}

// Back to display primaries. The result is already display encoded (2.2 gamma).
float3 AgXOutput(float3 color)
{
    const float3x3 outset = float3x3(
         1.19687900512017f,   -0.0980208811401368f, -0.0990297440797205f,
        -0.0528968517574562f,  1.15190312990417f,   -0.0989611768448433f,
        -0.0529716355144438f, -0.0980434501171241f,  1.15107367264116f);

    return saturate(mul(outset, color));
}

// Hides banding in smooth gradients (sky) after quantizing to 8 bits
float3 Dither(float3 color, float2 pixel)
{
    float noise = frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f)))); // Interleaved gradient noise
    return color + (noise - 0.5f) / 255.0f;
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target
{
    float3 color = hdrBuffer.Sample(input.uv).rgb * constants.exposure;

    color = AgXOutput(AgXLook(AgX(color)));

    // The swapchain uses a UNORM format, so the display encoded value is written as is
    return float4(Dither(color, input.position.xy), 1.0f);
}
