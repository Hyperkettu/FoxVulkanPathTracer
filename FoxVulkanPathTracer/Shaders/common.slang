// Declarations shared by the ray tracing stages (raygen, closest hit, miss).
// Included with #include, so everything here must be safe to repeat per stage.

#ifndef FOX_COMMON_SLANG
#define FOX_COMMON_SLANG

static const float PI = 3.14159265359f;

// Instance visibility bits, must match RayTracingScene::INSTANCE_MASK_* (RaytracingScene.h)
static const uint INSTANCE_MASK_OPAQUE = 0x01;
static const uint INSTANCE_MASK_GLASS = 0x02;       // Thin glass (windows)
static const uint INSTANCE_MASK_SOLID_GLASS = 0x04; // Refracting solids
static const uint INSTANCE_MASK_ALL = 0xFF;

// What the closest hit shader reports back to the path tracer in raygen.slang: a description
// of the surface at the hit point. All shading happens in the ray generation shader, so rays
// never recurse (the pipeline is created with a recursion depth of 1).
struct HitPayload
{
    float hitT;              // Distance along the ray, negative if the ray left the scene

    float3 geometricNormal;  // World space normal of the triangle
    float3 shadingNormal;    // World space normal after interpolation and normal mapping

    float3 baseColor;
    float metallic;
    float roughness;         // Perceptual roughness as authored (0 = mirror, 1 = fully rough)
    float transmission;      // 1 = clear glass
    float ior;               // Index of refraction of a solid; 0 = thin walled (a window pane)
    float3 absorption;       // Per meter inside a solid
    float specularWeight;    // Scales the dielectric specular reflection

    float3 emission;         // Emitted radiance
    int lightIndex;          // Entry in the light buffer if this surface is sampled as a light, else -1

    uint instanceIndex;      // Acceleration structure instance that was hit (an object, see RayTracingScene)
    float3 skinnedMotion;    // Deforming (skinned) surfaces: previous minus current position, world space
    float occlusion;         // Ambient occlusion texture: 1 = open, 0 = fully occluded
};

// Mirrors Fox::Graphics::Vulkan::Submesh (Vertex.h)
struct Submesh
{
    uint vertexOffset;
    uint indexOffset;
    uint vertexCount;
    uint indexCount;
    uint materialIndex;
    uint previousPositions; // Skinned: 1 + its first entry of gPreviousSkinnedPositions; 0 = not skinned
    uint pad1;
    uint pad2;
};

// Mirrors Fox::Graphics::Vulkan::Material (Material.h)
struct Material
{
    float4 baseColor;

    float3 emissive;
    float normalScale;

    float roughness;
    float metallic;
    float transmission;
    float specularWeight;

    int baseColorTexture;
    int metallicRoughnessTexture;
    int normalTexture;
    int emissiveTexture;

    float2 uvScale;
    float uvRotation;
    int lightIndex;

    // Separate floats: a float3 would be aligned to 16 bytes here, unlike in C++
    float ior;
    float absorptionR;
    float absorptionG;
    float absorptionB;

    int occlusionTexture;     // Red channel
    float occlusionStrength;
    float alphaCutoff;        // > 0: alpha tested (leaves), surfaces where base colour alpha < this are holes
    int layerMaterial;        // >= 0: terrain, its first layer material (see terrain.slang)
};

// Mirrors Fox::Graphics::Vulkan::Vertex (Vertex.h)
struct Vertex
{
    float4 position;
    float4 normal;
    float2 uv;
    float2 padding;
    float4 tangent; // xyz + handedness, all zero if the mesh has no tangents
};

[[vk::binding(3, 0)]] StructuredBuffer<Vertex> gAllVertices;
[[vk::binding(4, 0)]] StructuredBuffer<uint> gAllIndices;

// Every texture of the scene (bindless)
[[vk::binding(1, 1)]] Sampler2D gTextures[];

float4 SampleTexture(int index, float2 uv)
{
    return gTextures[NonUniformResourceIndex(index)].SampleLevel(uv, 0.0f);
}

// KHR_texture_transform: scale, then rotate (no offset)
float2 TransformUV(float2 uv, Material material)
{
    uv *= material.uvScale;

    if (material.uvRotation != 0.0f)
    {
        float s = sin(material.uvRotation);
        float c = cos(material.uvRotation);
        uv = float2(c * uv.x + s * uv.y, -s * uv.x + c * uv.y);
    }

    return uv;
}


// Shared by the closest hit shader and raygen (shadow rays through glass)
[[vk::binding(5, 0)]] StructuredBuffer<Submesh> gAllSubmeshes;
[[vk::binding(6, 0)]] StructuredBuffer<Material> gAllMaterials;

// Alpha testing: is this point of a triangle a hole (alpha below the material's cutoff)? Used by the
// any-hit shader and by shadow ray queries, for the parts of leaves the opacity micromaps leave undecided.
// submeshIndex: InstanceID() + GeometryIndex(); barycentrics: of vertices 1 and 2.
bool IsAlphaHole(uint submeshIndex, uint primitive, float2 barycentrics)
{
    Submesh submesh = gAllSubmeshes[submeshIndex];
    Material material = gAllMaterials[submesh.materialIndex];
    if (material.alphaCutoff <= 0.0f)
        return false;

    uint baseIndex = submesh.indexOffset + primitive * 3;
    float2 uv0 = gAllVertices[gAllIndices[baseIndex + 0]].uv;
    float2 uv1 = gAllVertices[gAllIndices[baseIndex + 1]].uv;
    float2 uv2 = gAllVertices[gAllIndices[baseIndex + 2]].uv;
    float2 uv = TransformUV(uv0 * (1.0f - barycentrics.x - barycentrics.y) + uv1 * barycentrics.x + uv2 * barycentrics.y, material);

    float alpha = material.baseColor.a;
    if (material.baseColorTexture >= 0)
        alpha *= SampleTexture(material.baseColorTexture, uv).a;
    return alpha < material.alphaCutoff;
}


// Mirrors Fox::Graphics::Vulkan::CameraUBO (ConstantBuffers.h).
struct Camera
{
    float3 camPos;
    float3 camForward;
    float3 camRight;
    float3 camUp;

    // The same vectors for the previous frame, used to compute motion vectors
    float3 prevCamPos;
    float3 prevCamForward;
    float3 prevCamRight;
    float3 prevCamUp;

    float2 resolution;
    float2 jitter;          // Sub-pixel offset of the primary ray from the pixel center, in pixels
    uint frameIndex;        // Monotonic frame counter, used to decorrelate the RNG between frames
    uint lightCount;        // Number of valid entries in the light buffer
    float tanHalfFov;       // tan(vertical field of view / 2)
    uint samplesPerPixel;   // Paths traced per pixel and frame
    uint jitterAllSamples;  // 1: every path uses `jitter`, 0: paths after the first are spread over the pixel
    uint gbufferFollowsReflections; // 1: the G-buffer describes what is seen in a mirror instead of the mirror

    int2 pickPixel;         // Report the object seen at this pixel in gPickResult; negative: none
    uint pickSlot;          // Entry of gPickResult to write (the frame in flight)
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

float2 NextRandomFloat2(inout uint state)
{
    float a = NextRandomFloat(state);
    float b = NextRandomFloat(state);
    return float2(a, b);
}

// Hashes the pixel coordinate and frame index into an initial RNG state
uint InitializePixelSeed(uint2 pixel, uint frameIndex)
{
    uint seed = pixel.x * 3266489917u + pixel.y * 668265263u + frameIndex * 4294967291u;

    seed = (seed ^ (seed >> 16u)) * 747796405u;
    seed = (seed ^ (seed >> 16u)) * 747796405u;
    return seed ^ (seed >> 16u);
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float MaxComponent(float3 v)
{
    return max(v.x, max(v.y, v.z));
}

// Builds an orthonormal basis around a unit vector (Duff et al., "Building an Orthonormal Basis, Revisited")
void BuildBasis(float3 n, out float3 tangent, out float3 bitangent)
{
    float s = n.z >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (s + n.z);
    float b = n.x * n.y * a;
    tangent = float3(1.0f + s * n.x * n.x * a, s * b, -s * n.x);
    bitangent = float3(b, s + n.y * n.y * a, -n.y);
}

// Balance between two sampling strategies for multiple importance sampling
float PowerHeuristic(float pdfA, float pdfB)
{
    float a = pdfA * pdfA;
    float b = pdfB * pdfB;
    return a / max(a + b, 1e-12f);
}

#endif
