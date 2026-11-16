// SVGF pass 4: multiply the albedo back onto the filtered lighting and save this
// frame's normal/depth for the next frame's reprojection test.

#include "denoise_common.slang"

[[vk::binding(0, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gIllumination;       // in
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gAlbedo;             // in
[[vk::binding(2, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> gNormalDepth;        // in
[[vk::binding(3, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> gNormalDepthHistory; // out
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gOutput;             // out
[[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gSpecular;           // in

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchId.xy;

    uint width, height;
    gOutput.GetDimensions(width, height);
    if (pixel.x >= width || pixel.y >= height)
        return;

    float4 normalDepth = gNormalDepth[pixel];
    float3 color = gIllumination[pixel].rgb;

    // The sky was never demodulated
    if (!IsSky(normalDepth.w))
    {
        color *= DemodulationAlbedo(gAlbedo[pixel].rgb, gSpecular[pixel].rgb);
    }

    gOutput[pixel] = float4(color, 1.0f);
    gNormalDepthHistory[pixel] = normalDepth;
}
