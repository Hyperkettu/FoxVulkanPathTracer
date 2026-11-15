// Declarations shared by the ray tracing stages (raygen, closest hit, miss).
// Included with #include, so everything here must be safe to repeat per stage.

#ifndef FOX_COMMON_SLANG
#define FOX_COMMON_SLANG

static const float PI = 3.14159265359f;

// Number of indirect bounces after the primary hit. The ray tracing pipeline is created with
// maxPipelineRayRecursionDepth = MAX_BOUNCES + 1 (see PipelineManager.cpp) - keep them in sync.
static const int MAX_BOUNCES = 4;

struct RayPayload
{
    half3 hitColor;
    int depth;
    uint rngState;
};

// Mirrors Fox::Graphics::Vulkan::CameraUBO (ConstantBuffers.h).
struct Camera
{
    float3 camPos;
    float3 camForward;
    float3 camRight;
    float3 camUp;
    float2 resolution;
    uint frameIndex;        // Monotonic frame counter, used to decorrelate the RNG between frames
    uint lightCount;        // Number of valid entries in the light buffer
    uint accumulatedFrames; // Frames already blended into the accumulation image (0 = restart)
};

// Core PCG hash function (transforms a 32-bit state)
uint PCG_Hash(uint state)
{
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Advances the LCG step and returns a pseudo-random uint
uint NextRandomUInt(inout uint state)
{
    state = state * 747796405u + 289133645u;
    return PCG_Hash(state);
}

// Returns a uniform float in [0.0, 1.0)
float NextRandomFloat(inout uint state)
{
    // Use the top 24 bits so the result is exactly representable and never rounds up to 1.0
    return float(NextRandomUInt(state) >> 8u) * (1.0f / 16777216.0f);
}

// Hashes the pixel coordinate and frame index into an initial RNG state
uint InitializePixelSeed(uint2 pixel, uint frameIndex)
{
    uint seed = pixel.x * 3266489917u + pixel.y * 668265263u + frameIndex * 4294967291u;

    seed = (seed ^ (seed >> 16u)) * 747796405u;
    seed = (seed ^ (seed >> 16u)) * 747796405u;
    return seed ^ (seed >> 16u);
}

#endif
