struct RayPayload
{
    float3 hitColor;
    int depth;
    uint rngState;
};

[[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;
[[vk::binding(1, 0)]] RWTexture2D<float4>             image;

struct Camera
{
    float3 camPos;
    float3 camForward;
    float3 camRight;
    float3 camUp;
    float2 resolution;
    uint frameIndex;
};

[[vk::binding(2, 0)]] ConstantBuffer<Camera> camera;

// 1. Core PCG hash function (transforms a 32-bit state)
uint PCG_Hash(uint state) {
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// 2. Advances the LCG step and returns a pseudo-random uint
uint NextRandomUInt(inout uint state) {
    state = state * 747796405u + 289133645u;
    return PCG_Hash(state);
}

// 3. Returns a high-quality uniform float between [0.0, 1.0)
float NextRandomFloat(inout uint state) {
    return float(NextRandomUInt(state)) / 4294967296.0;
}

// A fast 2D coordinate hashing function to seed the PCG state
uint InitializePixelSeed(uint2 pixel, uint frameIndex) {
    // Basic block hash scramble
    uint x = pixel.x;
    uint y = pixel.y;
    uint seed = x * 3266489917u + y * 668265263u + frameIndex * 4294967291u;
    
    // Scramble the bits via a simplified PCG pipeline step
    seed = (seed ^ (seed >> 16u)) * 747796405u;
    seed = (seed ^ (seed >> 16u)) * 747796405u;
    return seed ^ (seed >> 16u);
}

[shader("raygeneration")]
void main()
{
    uint2 pixel = DispatchRaysIndex().xy;

    uint frameIndex = camera.frameIndex;

    // Better initial scrambling seed using LCG values
    uint seed = InitializePixelSeed(pixel, frameIndex);

    float3 color = float3(0, 0, 0);
    float aspectRatio = camera.resolution.x / camera.resolution.y;

    uint MONTE_CARLO_SAMPLES_PER_PIXEL = 2;

    for(uint i = 0; i < MONTE_CARLO_SAMPLES_PER_PIXEL; i++) {
        float2 offset = float2(NextRandomFloat(seed), NextRandomFloat(seed)) - 0.5f;

        // Convert pixel to -1.0 to 1.0 range
        float2 d = (((float2)pixel + offset) / camera.resolution) * 2.0f - 1.0f;
        d.y = -d.y;         // Flip Y coordinate for Vulkan/Slang convention
        d.x *= aspectRatio; // Apply aspect ratio correctly to the screen coordinates

        // Compute the Ray Direction using uniform vectors
        float3 origin = camera.camPos;
        float3 dir = normalize(camera.camForward + d.x * camera.camRight + d.y * camera.camUp);
                            
        RayPayload payload;
        payload.hitColor = float3(0.0f);
        payload.depth = 0;
        payload.rngState = seed; // Pass the active seed state into the ray

        RayDesc ray;
        ray.Origin    = origin;
        ray.Direction = dir;
        ray.TMin      = 0.001f;
        ray.TMax      = 10000.0f;

        TraceRay(
            tlas, 
            RAY_FLAG_FORCE_OPAQUE, 
            0xFF, 
            0, 0, 0, 
            ray, 
            payload
        );

        color += payload.hitColor;
        seed = payload.rngState; // Retrieve mutated seed state back for the next iteration
    }

    color /= float(MONTE_CARLO_SAMPLES_PER_PIXEL);

    image[pixel] = float4(color, 1.0f);
}