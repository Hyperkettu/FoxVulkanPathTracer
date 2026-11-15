#include "common.slang"

// Mirrors Fox::Graphics::Vulkan::Environment (ConstantBuffers.h).
struct Environment
{
    uint environmentMapTextureIndex;
    float environmentMapIntensity;
    int pad0;
    int pad1;
}

[[vk::binding(0, 1)]] ConstantBuffer<Environment> environment;
[[vk::binding(1, 1)]] Sampler2D gTextures[];

// Equirectangular lookup: +Y is the top row of the map.
float3 SampleHDRIEnvironmentMap(float3 direction)
{
    direction = normalize(direction);
    float u = atan2(direction.z, direction.x) / (2.0f * PI) + 0.5f;
    float v = acos(clamp(direction.y, -1.0f, 1.0f)) / PI;
    return gTextures[NonUniformResourceIndex(environment.environmentMapTextureIndex)].SampleLevel(float2(u, v), 0.0f).xyz;
}

[shader("miss")]
void main(inout RayPayload payload)
{
    float3 radiance = SampleHDRIEnvironmentMap(WorldRayDirection()) * environment.environmentMapIntensity;

    // The payload is half precision: clamp so the sun in an HDR map cannot overflow to infinity.
    payload.hitColor = half3(min(radiance, float3(60000.0f)));
}
