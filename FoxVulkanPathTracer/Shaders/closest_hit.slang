// Closest hit: looks up the geometry and material at the hit point and hands a description of
// the surface back to the path tracer (raygen.slang). No lighting is computed here.

#include "common.slang"

[[vk::binding(2, 0)]] ConstantBuffer<Camera> camera;

#include "surface.slang"

[shader("closesthit")]
void main(inout HitPayload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    // Each submesh is one geometry of its acceleration structure, and the instance's custom index is the
    // index of its first submesh (see RayTracingScene::UploadGeometry)
    float3 barycentrics = float3(
        1.0f - attribs.barycentrics.x - attribs.barycentrics.y,
        attribs.barycentrics.x,
        attribs.barycentrics.y);

    DescribeSurface(payload, InstanceID() + GeometryIndex(), PrimitiveIndex(), barycentrics,
        (float3x3)ObjectToWorld3x4(), (float3x3)WorldToObject3x4(),
        WorldRayOrigin() + WorldRayDirection() * RayTCurrent(), InstanceIndex(), RayTCurrent());
}
