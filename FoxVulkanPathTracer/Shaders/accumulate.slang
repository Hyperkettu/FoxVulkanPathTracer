// Reference accumulation: running average of all frames since the view last changed.

[[vk::binding(0, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gRadiance;      // in
[[vk::binding(1, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> gAccumulation;  // in/out

struct Constants
{
    uint accumulatedFrames; // Frames already in the average (0 = start over)
};

[[vk::push_constant]] Constants constants;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchId.xy;

    uint width, height;
    gAccumulation.GetDimensions(width, height);
    if (pixel.x >= width || pixel.y >= height)
        return;

    float3 color = gRadiance[pixel].rgb;

    if (constants.accumulatedFrames > 0)
    {
        float3 history = gAccumulation[pixel].rgb;
        color = lerp(history, color, 1.0f / float(constants.accumulatedFrames + 1));
    }

    gAccumulation[pixel] = float4(color, 1.0f);
}
