// The hybrid path tracer: merges the software rasterizer's nearest surfaces (hybrid_swraster.slang) into the hardware's
// visibility buffer, pixel by pixel, by depth; and writes the depth pyramid's level 0 (the nearer of the two).

#include "hybrid_common.slang"

[[vk::binding(0, 0)]] Texture2D<float> gDepth;                                     // The hardware raster's
[[vk::binding(1, 0)]] StructuredBuffer<uint64_t> gSoftware;                         // depth << 32 | record << 8 | triangle
[[vk::binding(2, 0)]] StructuredBuffer<DrawInfo> gDrawInfos;
[[vk::binding(3, 0)]] [[vk::image_format("rg32ui")]] RWTexture2D<uint2> gVisibility;
[[vk::binding(4, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> gPyramid;    // Level 0

struct Constants
{
    uint2 size;
};
[[vk::push_constant]] ConstantBuffer<Constants> constants;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= constants.size))
        return;
    float hardware = gDepth.Load(int3(id.xy, 0));
    uint64_t software = gSoftware[id.y * constants.size.x + id.x];
    float softwareDepth = asfloat(uint(software >> 32));
    if (software != 0 && softwareDepth > hardware)
    {
        uint payload = uint(software & 0xFFFFFFFF);
        uint record = payload >> 8;
        DrawInfo info = gDrawInfos[record];
        gVisibility[id.xy] = uint2(record + 1, info.firstIndex / 3 + (payload & 0xFF));
        gPyramid[id.xy] = softwareDepth;
    }
    else
    {
        gPyramid[id.xy] = hardware;
    }
}
