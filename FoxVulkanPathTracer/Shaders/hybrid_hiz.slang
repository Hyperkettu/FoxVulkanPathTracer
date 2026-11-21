// The hybrid path tracer's depth pyramid (HiZ): level 0 is the visibility buffer's depth (reversed: 1 near, 0 far,
// 0 also where nothing was drawn), each further level half the size, each texel the farthest (smallest) depth of the
// 2x2 texels under it. Levels round up (ceil), so every texel of a level is covered by the one above it.
// Cluster culling tests against it; the path tracer's screen traces march through level 0.

[[vk::binding(0, 0)]] Texture2D<float> gSource;                                  // The depth buffer, or the level below
[[vk::binding(1, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> gTarget;

struct Constants
{
    uint2 targetSize;
    uint2 sourceSize;
};
[[vk::push_constant]] ConstantBuffer<Constants> constants;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= constants.targetSize))
        return;
    if (all(constants.sourceSize == constants.targetSize))
    {
        gTarget[id.xy] = gSource.Load(int3(id.xy, 0));
        return;
    }
    int2 last = int2(constants.sourceSize) - 1;
    int2 p = int2(id.xy) * 2;
    float a = gSource.Load(int3(min(p, last), 0));
    float b = gSource.Load(int3(min(p + int2(1, 0), last), 0));
    float c = gSource.Load(int3(min(p + int2(0, 1), last), 0));
    float d = gSource.Load(int3(min(p + int2(1, 1), last), 0));
    gTarget[id.xy] = min(min(a, b), min(c, d));
}
