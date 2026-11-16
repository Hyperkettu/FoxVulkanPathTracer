// SVGF pass 2: variance estimation.
// The temporal variance is unreliable for the first few frames after a disocclusion. For those
// pixels the variance is estimated spatially instead, with a 7x7 bilateral filter.

#include "denoise_common.slang"

[[vk::binding(0, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gIllumination;    // in  (rgb: lighting, a: temporal variance)
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gMoments;         // in
[[vk::binding(2, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> gNormalDepth;     // in
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gIlluminationOut; // out
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gMomentsHistory;  // out (copy for the next frame)

static const float SPATIAL_ESTIMATE_FRAMES = 4.0f; // History length below which the spatial estimate is used
static const int RADIUS = 3;
static const float NORMAL_POWER = 128.0f;

float DepthSlope(int2 pixel, int2 size, float depth)
{
    // Smallest of the forward/backward differences, so a silhouette on one side does not inflate it
    float dx0 = abs(gNormalDepth[clamp(pixel + int2(1, 0), int2(0), size - 1)].w - depth);
    float dx1 = abs(gNormalDepth[clamp(pixel - int2(1, 0), int2(0), size - 1)].w - depth);
    float dy0 = abs(gNormalDepth[clamp(pixel + int2(0, 1), int2(0), size - 1)].w - depth);
    float dy1 = abs(gNormalDepth[clamp(pixel - int2(0, 1), int2(0), size - 1)].w - depth);
    return max(min(dx0, dx1), min(dy0, dy1));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchId.xy);

    uint width, height;
    gIlluminationOut.GetDimensions(width, height);
    int2 size = int2(width, height);
    if (any(pixel >= size))
        return;

    float4 illumination = gIllumination[pixel];
    float4 moments = gMoments[pixel];
    float4 normalDepth = gNormalDepth[pixel];

    gMomentsHistory[pixel] = moments;

    float historyLength = moments.z;

    if (IsSky(normalDepth.w) || historyLength >= SPATIAL_ESTIMATE_FRAMES)
    {
        gIlluminationOut[pixel] = illumination;
        return;
    }

    float depthSlope = DepthSlope(pixel, size, normalDepth.w);

    float weightSum = 1.0f;
    float3 illuminationSum = illumination.rgb;
    float2 momentsSum = moments.xy;

    for (int y = -RADIUS; y <= RADIUS; y++)
    {
        for (int x = -RADIUS; x <= RADIUS; x++)
        {
            if (x == 0 && y == 0)
                continue;

            int2 tap = pixel + int2(x, y);
            if (any(tap < int2(0)) || any(tap >= size))
                continue;

            float4 tapNormalDepth = gNormalDepth[tap];
            if (IsSky(tapNormalDepth.w))
                continue;

            float weight = NormalWeight(normalDepth.xyz, tapNormalDepth.xyz, NORMAL_POWER)
                         * DepthWeight(normalDepth.w, tapNormalDepth.w, depthSlope, length(float2(x, y)));

            illuminationSum += weight * gIllumination[tap].rgb;
            momentsSum += weight * gMoments[tap].xy;
            weightSum += weight;
        }
    }

    illuminationSum /= weightSum;
    momentsSum /= weightSum;

    float variance = max(0.0f, momentsSum.y - momentsSum.x * momentsSum.x);

    // Few samples went into this estimate: bias the variance up so the a-trous filter blurs more
    variance *= SPATIAL_ESTIMATE_FRAMES / historyLength;

    gIlluminationOut[pixel] = float4(illuminationSum, variance);
}
