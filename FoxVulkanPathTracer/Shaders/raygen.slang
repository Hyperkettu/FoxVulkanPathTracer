struct RayPayload
{
    float3 hitColor;
    int depth;
    uint rngState;
};

// Bindings translated to Slang / Vulkan layout style
[[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;
[[vk::binding(1, 0)]] RWTexture2D<float4>             image;

struct Camera
{
    float3 camPos;
    float3 camForward;
    float3 camRight;
    float3 camUp;
    float2 resolution;
};

[[vk::binding(2, 0)]] ConstantBuffer<Camera> camera;

float NextRandomFloat(inout uint state) {
    state = state * 1664525u + 1013904223u;
    return float(state & 0x00FFFFFFu) / float(0x01000000u);
}

[shader("raygeneration")]
void main()
{
    // Slang equivalents to gl_LaunchIDEXT and gl_LaunchSizeEXT
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 size  = DispatchRaysDimensions().xy;

    uint seed =  pixel.x * 1973 + pixel.y * 9277;

    float2 offset = float2(NextRandomFloat(seed), NextRandomFloat(seed)) - 0.5;

    // 1. Convert pixel to -1.0 to 1.0 range (Normalized Device Coordinates)
    float2 d = (((float2)pixel + offset) / camera.resolution) * 2.0f - 1.0f;
    d.y = -d.y; // Flip Y coordinate for correct orientation    

    float aspectRatio = camera.resolution.x / camera.resolution.y;

    // 2. Compute the Ray Direction
    float3 origin = camera.camPos;
    float3 dir = normalize(camera.camForward + 
                           d.x * camera.camRight * aspectRatio + 
                           d.y * camera.camUp);
                            
    // Initialize the ray payload
    RayPayload payload;
    payload.hitColor = float3(0.0f);
    payload.depth = 0;
    payload.rngState = seed;

    // Setup the Ray Descriptor struct expected by TraceRay
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = dir;
    ray.TMin      = 0.001f;
    ray.TMax      = 10000.0f;

    // 3. Trace the ray
    // RAY_FLAG_FORCE_OPAQUE maps to gl_RayFlagsOpaqueEXT
    TraceRay(
        tlas, 
        RAY_FLAG_FORCE_OPAQUE, 
        0xFF, 
        0, 0, 0, // sbtRecordOffset, sbtRecordStride, missIndex
        ray, 
        payload
    );

    // Write back to the storage image
    image[pixel] = float4(payload.hitColor, 1.0f);
}