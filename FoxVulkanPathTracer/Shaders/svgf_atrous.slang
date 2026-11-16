// SVGF pass 3: edge-avoiding a-trous wavelet filter.
// One iteration of a 5x5 B-spline kernel whose taps are stepSize pixels apart. Running it with
// step sizes 1, 2, 4, 8, 16 covers a large area cheaply. Taps are weighted by how similar their
// depth, normal and luminance are to the center; the luminance tolerance scales with the
// estimated standard deviation, so noisy regions are blurred and converged ones are left alone.

#include "denoise_common.slang"

[[vk::binding(0, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gIllumination;        // in  (rgb: lighting, a: variance)
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gIlluminationOut;     // out
[[vk::binding(2, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> gNormalDepth;         // in
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gIlluminationHistory; // out (first iteration only)

struct Constants
{
    int stepSize;
    uint writeHistory;
};

[[vk::push_constant]] Constants constants;

static const float PHI_COLOR = 10.0f;    // Luminance tolerance in standard deviations
static const float NORMAL_POWER = 128.0f;

// Variance is itself noisy: smooth it with a 3x3 Gaussian before using it to steer the filter
float FilteredVariance(int2 pixel, int2 size)
{
    const float kernel[2][2] = {
        { 1.0f / 4.0f, 1.0f / 8.0f },
        { 1.0f / 8.0f, 1.0f / 16.0f }
    };

    float sum = 0.0f;

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            int2 tap = clamp(pixel + int2(x, y), int2(0), size - 1);
            sum += kernel[abs(x)][abs(y)] * gIllumination[tap].a;
        }
    }

    return sum;
}

float DepthSlope(int2 pixel, int2 size, float depth)
{
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

    float4 center = gIllumination[pixel];
    float4 centerNormalDepth = gNormalDepth[pixel];

    float4 result = center;

    if (!IsSky(centerNormalDepth.w))
    {
        const float kernelWeights[3] = { 3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f };

        float centerLuminance = Luminance(center.rgb);
        float luminanceTolerance = PHI_COLOR * sqrt(max(0.0f, FilteredVariance(pixel, size))) + 1e-4f;
        float depthSlope = DepthSlope(pixel, size, centerNormalDepth.w);

        float weightSum = kernelWeights[0] * kernelWeights[0];
        float3 illuminationSum = center.rgb * weightSum;
        float varianceSum = center.a * weightSum * weightSum;

        for (int y = -2; y <= 2; y++)
        {
            for (int x = -2; x <= 2; x++)
            {
                if (x == 0 && y == 0)
                    continue;

                int2 offset = int2(x, y) * constants.stepSize;
                int2 tap = pixel + offset;
                if (any(tap < int2(0)) || any(tap >= size))
                    continue;

                float4 tapNormalDepth = gNormalDepth[tap];
                if (IsSky(tapNormalDepth.w))
                    continue;

                float4 tapIllumination = gIllumination[tap];

                float luminanceWeight = exp(-abs(centerLuminance - Luminance(tapIllumination.rgb)) / luminanceTolerance);

                float weight = kernelWeights[abs(x)] * kernelWeights[abs(y)]
                             * NormalWeight(centerNormalDepth.xyz, tapNormalDepth.xyz, NORMAL_POWER)
                             * DepthWeight(centerNormalDepth.w, tapNormalDepth.w, depthSlope, length(float2(offset)))
                             * luminanceWeight;

                illuminationSum += weight * tapIllumination.rgb;
                // Var(sum of w_i * x_i) = sum of w_i^2 * Var(x_i)
                varianceSum += weight * weight * tapIllumination.a;
                weightSum += weight;
            }
        }

        result = float4(illuminationSum / weightSum, varianceSum / (weightSum * weightSum));
    }

    gIlluminationOut[pixel] = result;

    // Feeding the lightly filtered result back as history (instead of the raw temporal
    // accumulation) is what gives SVGF its stability.
    if (constants.writeHistory != 0)
    {
        gIlluminationHistory[pixel] = result;
    }
}
