// SVGF pass 1: temporal accumulation.
// Finds where each pixel was in the previous frame, checks that it is still the same surface
// and blends the new (demodulated) lighting sample into the reprojected history.

#include "denoise_common.slang"

[[vk::binding(0, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gRadiance;            // in
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gAlbedo;              // in
[[vk::binding(2, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> gNormalDepth;         // in
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gMotion;              // in
[[vk::binding(4, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> gNormalDepthHistory;  // in
[[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gIlluminationHistory; // in
[[vk::binding(6, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gMomentsHistory;      // in
[[vk::binding(7, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gIlluminationOut;     // out
[[vk::binding(8, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gMomentsOut;          // out
[[vk::binding(9, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gSpecular;            // in

struct Constants
{
    uint resetHistory;
};

[[vk::push_constant]] Constants constants;

// Lower bounds of the blend factor: how much a new sample always contributes.
// Smaller = smoother but slower to react to lighting changes.
static const float MIN_COLOR_ALPHA = 0.05f;
static const float MIN_MOMENTS_ALPHA = 0.2f;
static const float MAX_HISTORY_LENGTH = 64.0f;

// Is the history sample at prevPixel the same surface as the current pixel?
bool IsHistoryValid(int2 prevPixel, int2 size, float3 normal, float expectedPrevDepth, out float4 prevNormalDepth)
{
    prevNormalDepth = float4(0.0f);

    if (any(prevPixel < int2(0)) || any(prevPixel >= size))
        return false;

    prevNormalDepth = gNormalDepthHistory[prevPixel];

    if (IsSky(prevNormalDepth.w))
        return false;

    if (abs(prevNormalDepth.w - expectedPrevDepth) > 0.1f * expectedPrevDepth)
        return false;

    if (dot(prevNormalDepth.xyz, normal) < 0.9f)
        return false;

    return true;
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

    float4 normalDepth = gNormalDepth[pixel];
    float3 radiance = gRadiance[pixel].rgb;

    // The sky is noise free: pass it through untouched.
    if (IsSky(normalDepth.w))
    {
        float skyLuminance = Luminance(radiance);
        gIlluminationOut[pixel] = float4(radiance, 0.0f);
        gMomentsOut[pixel] = float4(skyLuminance, skyLuminance * skyLuminance, 1.0f, 0.0f);
        return;
    }

    // Demodulate: filter lighting only, the albedo is multiplied back in after filtering.
    float3 illumination = radiance / DemodulationAlbedo(gAlbedo[pixel].rgb, gSpecular[pixel].rgb);
    float luminance = Luminance(illumination);

    float3 normal = normalDepth.xyz;
    float4 motion = gMotion[pixel];
    float expectedPrevDepth = motion.z;

    float3 historyIllumination = float3(0.0f);
    float3 historyMoments = float3(0.0f); // x: mean, y: mean of squares, z: history length
    bool hasHistory = false;

    if (constants.resetHistory == 0)
    {
        // Position of this surface point in the previous frame, in pixel-center coordinates
        float2 prevPosition = float2(pixel) + motion.xy;
        int2 prevBase = int2(floor(prevPosition));
        float2 fraction = prevPosition - float2(prevBase);

        // Bilinear filter over the 2x2 footprint, using only taps that pass the surface test
        const int2 offsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };
        const float weights[4] = {
            (1.0f - fraction.x) * (1.0f - fraction.y),
            fraction.x * (1.0f - fraction.y),
            (1.0f - fraction.x) * fraction.y,
            fraction.x * fraction.y
        };

        float weightSum = 0.0f;

        for (int i = 0; i < 4; i++)
        {
            int2 tap = prevBase + offsets[i];
            float4 prevNormalDepth;

            if (IsHistoryValid(tap, size, normal, expectedPrevDepth, prevNormalDepth))
            {
                historyIllumination += weights[i] * gIlluminationHistory[tap].rgb;
                historyMoments += weights[i] * gMomentsHistory[tap].rgb;
                weightSum += weights[i];
            }
        }

        // Nothing usable in the 2x2 footprint (e.g. thin geometry): search the 3x3 neighbourhood
        if (weightSum < 0.01f)
        {
            historyIllumination = float3(0.0f);
            historyMoments = float3(0.0f);
            weightSum = 0.0f;

            int2 prevNearest = int2(round(prevPosition));

            for (int y = -1; y <= 1; y++)
            {
                for (int x = -1; x <= 1; x++)
                {
                    int2 tap = prevNearest + int2(x, y);
                    float4 prevNormalDepth;

                    if (IsHistoryValid(tap, size, normal, expectedPrevDepth, prevNormalDepth))
                    {
                        historyIllumination += gIlluminationHistory[tap].rgb;
                        historyMoments += gMomentsHistory[tap].rgb;
                        weightSum += 1.0f;
                    }
                }
            }
        }

        if (weightSum >= 0.01f)
        {
            historyIllumination /= weightSum;
            historyMoments /= weightSum;
            hasHistory = true;
        }
    }

    float historyLength = hasHistory ? min(historyMoments.z + 1.0f, MAX_HISTORY_LENGTH) : 1.0f;

    // Plain average while the history is short, exponential moving average afterwards
    float colorAlpha = hasHistory ? max(1.0f / historyLength, MIN_COLOR_ALPHA) : 1.0f;
    float momentsAlpha = hasHistory ? max(1.0f / historyLength, MIN_MOMENTS_ALPHA) : 1.0f;

    float2 moments = lerp(historyMoments.xy, float2(luminance, luminance * luminance), momentsAlpha);
    float variance = max(0.0f, moments.y - moments.x * moments.x);

    float3 integrated = lerp(historyIllumination, illumination, colorAlpha);

    gIlluminationOut[pixel] = float4(integrated, variance);
    gMomentsOut[pixel] = float4(moments, historyLength, 0.0f);
}
