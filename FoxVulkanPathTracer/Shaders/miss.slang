static float PI = 3.14159265358f;

struct RayPayload
{
    half3 hitColor;
    int depth;
    uint rngState;
};

struct Environment {
    uint environmentMapTextureIndex;
    float environmentMapIntensity;
    int pad0;
    int pad1;
}

[[vk::binding(0, 1)]] ConstantBuffer<Environment> environment;
[[vk::binding(1, 1)]] Sampler2D gTextures[];


half3 SampleHDRIEnvironmentMap(float3 direction) {
    direction = normalize(direction);
    float u = atan2(direction.z, direction.x) / (2.0f * PI) + 0.5f;
    float v = acos(clamp(direction.y, 0.0f, 1.0f)) / PI;
    return half3(gTextures[environment.environmentMapTextureIndex].SampleLevel(float2(u, v), 0.0f).xyz);
}

[shader("miss")]
void main(inout RayPayload payload)
{
    payload.hitColor = SampleHDRIEnvironmentMap(WorldRayDirection()) * half(environment.environmentMapIntensity);
}