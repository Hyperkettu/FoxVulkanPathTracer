// Any hit: alpha testing. Runs only for non-opaque geometry (alpha tested materials such as leaves), and
// with opacity micromaps only where the micromap could not decide (micro-triangles on a leaf's outline):
// everywhere else the hardware already knows whether the point is leaf or hole.

#include "common.slang"

[shader("anyhit")]
void main(inout HitPayload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    if (IsAlphaHole(InstanceID() + GeometryIndex(), PrimitiveIndex(), attribs.barycentrics))
        IgnoreHit();
}
