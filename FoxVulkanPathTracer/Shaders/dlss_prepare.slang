// Repacks the G-buffer into the guide buffers DLSS Ray Reconstruction expects:
// one value per image where the renderer packs several, and its preferred values for sky pixels.

#include "denoise_common.slang"

[[vk::binding(0, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> gNormalDepth;        // in
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gSpecular;           // in
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gMotion;             // in
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gNormalRoughnessOut; // xyz: world normal, w: roughness
[[vk::binding(4, 0)]] [[vk::image_format("r32f")]]    RWTexture2D<float>  gDepthOut;           // View space depth
[[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gMotionOut;          // xy: pixels to the previous position
[[vk::binding(6, 0)]] [[vk::image_format("r32f")]]    RWTexture2D<float>  gHitDistanceOut;     // Specular hit distance

struct Constants
{
    float farPlane; // Depth reported for the sky
};

[[vk::push_constant]] Constants constants;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchId.xy;

    uint width, height;
    gDepthOut.GetDimensions(width, height);
    if (pixel.x >= width || pixel.y >= height)
        return;

    float4 normalDepth = gNormalDepth[pixel];
    float4 motion = gMotion[pixel];
    bool isSky = IsSky(normalDepth.w);

    gNormalRoughnessOut[pixel] = isSky ? float4(0.0f) : float4(normalDepth.xyz, gSpecular[pixel].a);
    gDepthOut[pixel] = isSky ? constants.farPlane : normalDepth.w;
    gMotionOut[pixel] = float4(motion.xy, 0.0f, 0.0f);
    gHitDistanceOut[pixel] = isSky ? 65504.0f : motion.w;
}
