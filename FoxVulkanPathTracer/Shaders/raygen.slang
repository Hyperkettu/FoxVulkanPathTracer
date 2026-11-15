#include "common.slang"

[[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;

// Linear HDR accumulation target. Holds the running average of every frame rendered since the
// camera last moved; the tone mapping pass reads it afterwards.
[[vk::binding(1, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> image;

[[vk::binding(2, 0)]] ConstantBuffer<Camera> camera;

static const uint SAMPLES_PER_PIXEL = 4;

[shader("raygeneration")]
void main()
{
    uint2 pixel = DispatchRaysIndex().xy;

    uint seed = InitializePixelSeed(pixel, camera.frameIndex);

    float3 color = float3(0.0f);
    float aspectRatio = camera.resolution.x / camera.resolution.y;

    for (uint i = 0; i < SAMPLES_PER_PIXEL; i++)
    {
        // Jitter inside the pixel for anti-aliasing
        float2 offset = float2(NextRandomFloat(seed), NextRandomFloat(seed));

        float2 d = (((float2)pixel + offset) / camera.resolution) * 2.0f - 1.0f;
        d.y = -d.y;
        d.x *= aspectRatio;

        RayPayload payload;
        payload.hitColor = half3(0.0h);
        payload.depth = 0;
        payload.rngState = seed;

        RayDesc ray;
        ray.Origin    = camera.camPos;
        ray.Direction = normalize(camera.camForward + d.x * camera.camRight + d.y * camera.camUp);
        ray.TMin      = 0.001f;
        ray.TMax      = 10000.0f;

        TraceRay(tlas, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 0, 0, ray, payload);

        color += float3(payload.hitColor);
        seed = payload.rngState;
    }

    color /= float(SAMPLES_PER_PIXEL);

    // Progressive refinement: blend this frame into the running average.
    if (camera.accumulatedFrames > 0)
    {
        float3 history = image[pixel].rgb;
        color = lerp(history, color, 1.0f / float(camera.accumulatedFrames + 1));
    }

    image[pixel] = float4(color, 1.0f);
}
