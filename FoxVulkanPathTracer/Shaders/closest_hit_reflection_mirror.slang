static const float PI = 3.14159265359;

enum class LightType
{ 
    Directional = 0, 
    Point, 
    Spot, 
    QuadAreaLight
};

struct Vertex
{
    float4 position;
    float4 normal;
    float2 uv;
    float2 padding;
};

struct Submesh
{
    uint vertexOffset;   // Optional for global indices
    uint indexOffset;
    uint vertexCount;
    uint indexCount;
};

struct Material {
    float4 albedo;                         // Offset: 0   (16 bytes)
    float2 roughnessMetallic;              // Offset: 16  (8 bytes)
    uint albedoTextureIndex;               // Offset: 24  (4 bytes)
    uint roughnessMetallicTextureIndex;    // Offset: 28  (4 bytes)
    float3 emissiveColor;                  // Offset: 32  (12 bytes)
    int emissiveTextureIndex;              // Offset: 44  (4 bytes)
    float intensity;                       // Offset: 48  (4 bytes)
    float padding;                         // Offset: 52  (4 bytes)
};

struct Light {
    float3 position;  
    float  _pad0;      // Fills the rest of the 16-byte boundary for position
    
    float3 normal;    
    float  _pad1;      // Fills the rest of the 16-byte boundary for normal
    
    float3 tangentX;  
    float  _pad2;      // Fills the rest of the 16-byte boundary for tangentX
    
    float3 tangentY;  
    float  _pad3;      // Fills the rest of the 16-byte boundary for tangentY
    
    float3 color;     
    float  _pad4;      // Fills the rest of the 16-byte boundary for color
    
    float  area;       // 4 bytes (alignas(4) in C++)
    int    type;       // 4 bytes (alignas(4) in C++)
    
    int2   _pad5;      // Pads the end of the entire struct to a multiple of 16 bytes 
                       // (Ensures safe array indexing in StructuredBuffers)
};

[[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;

[[vk::binding(3, 0)]] StructuredBuffer<Vertex> gAllVertices;
[[vk::binding(4, 0)]] StructuredBuffer<uint> gAllIndices;
[[vk::binding(5, 0)]] StructuredBuffer<Submesh> gAllSubmeshes;
[[vk::binding(6, 0)]] StructuredBuffer<Material> gAllMaterials;
[[vk::binding(7, 0)]] StructuredBuffer<Light> gAllLights;

struct RayPayload
{
    float3 hitColor;
    int depth;
    uint rngState;
};

struct ShadowPayload {
    bool isShadowed;
}; // Fixed: Added missing semicolon

uint PCG_Hash(uint state) {
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint NextRandomUInt(inout uint state) {
    state = state * 747796405u + 289133645u;
    return PCG_Hash(state);
}

// 3. Returns a high-quality uniform float between [0.0, 1.0)
float NextRandomFloat(inout uint state) {
    return float(NextRandomUInt(state)) / 4294967296.0;
}

float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    return a2 / (PI * pow(NdotH * NdotH * (a2 - 1.0) + 1.0, 2.0) + 0.000001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float k = pow(roughness + 1.0, 2.0) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + 0.000001);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 SafeOffsetRayOrigin(float3 p, float3 n) {
    const float origin = 1.0f / 32.0f;
    const float float_scale = 1.0f / 65536.0f;
    const float int_scale = 256.0f;

    float3 of_i = float3(int_scale * n.x, int_scale * n.y, int_scale * n.z);
    float3 p_i = float3(
        asfloat(asint(p.x) + (p.x < 0 ? -of_i.x : of_i.x)),
        asfloat(asint(p.y) + (p.y < 0 ? -of_i.y : of_i.y)),
        asfloat(asint(p.z) + (p.z < 0 ? -of_i.z : of_i.z))
    );

    return float3(abs(p.x) < origin ? p.x + float_scale * n.x : p_i.x,
                  abs(p.y) < origin ? p.y + float_scale * n.y : p_i.y,
                  abs(p.z) < origin ? p.z + float_scale * n.z : p_i.z);
}

[shader("closesthit")]
void main(
    inout RayPayload payload,
    BuiltInTriangleIntersectionAttributes attribs)
{
    uint submeshID = InstanceID();
    uint primitiveID = PrimitiveIndex();

    Submesh submesh = gAllSubmeshes[submeshID];
    Material material = gAllMaterials[submeshID];

    // Triangle indices
    uint baseIndex = submesh.indexOffset + primitiveID * 3;

    uint i0 = gAllIndices[baseIndex + 0];
    uint i1 = gAllIndices[baseIndex + 1];
    uint i2 = gAllIndices[baseIndex + 2];

    Vertex v0 = gAllVertices[i0];
    Vertex v1 = gAllVertices[i1];
    Vertex v2 = gAllVertices[i2];

    float3 barycentrics = float3(
        1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
        attribs.barycentrics.x,
        attribs.barycentrics.y);

    float3 localNormal =
        normalize(
            v0.normal.xyz * barycentrics.x +
            v1.normal.xyz * barycentrics.y +
            v2.normal.xyz * barycentrics.z);

    float2 uv = v0.uv.xy * barycentrics.x + 
                 v1.uv.xy * barycentrics.y + 
                 v2.uv.xy * barycentrics.z;

    float3x3 normalMatrix = ((float3x3)ObjectToWorld3x4());

    float3 N = normalize(mul(normalMatrix, localNormal));
    float3 worldPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    
    float3 albedo = material.albedo.xyz;

    // Check depth budget to prevent infinite recursion loop
    if (payload.depth <= 4)
    {
        // Setup payload for the reflection bounce ray
        RayPayload bouncePayload;
        bouncePayload.hitColor = float3(0.0f);
        bouncePayload.depth = payload.depth + 1;
        bouncePayload.rngState = payload.rngState;

        // Perfect mirror reflection vector calculation
        float3 reflectDir = reflect(WorldRayDirection(), N);

        // Standard RayDesc setup
        RayDesc ray;
        ray.Origin = worldPos + 0.001f * N;
        ray.Direction = reflectDir;
        ray.TMin = 0.001f;
        ray.TMax = 10000.0f;

        // Trace the secondary reflection ray
        TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, bouncePayload);

        // Mix the reflection return color with the surface properties
        payload.hitColor = material.emissiveColor + (albedo * bouncePayload.hitColor);
        payload.rngState = bouncePayload.rngState;
    }
    else
    {
        // Fallback color if the reflection bounce limit is reached
        payload.hitColor = material.emissiveColor;
    }
}