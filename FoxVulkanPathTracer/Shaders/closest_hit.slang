
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

[[vk::binding(3, 0)]]
StructuredBuffer<Vertex> gAllVertices;

[[vk::binding(4, 0)]]
StructuredBuffer<uint> gAllIndices;

[[vk::binding(5, 0)]]
StructuredBuffer<Submesh> gAllSubmeshes;

[[vk::binding(6, 0)]]
StructuredBuffer<Material> gAllMaterials;

[[vk::binding(7, 0)]]
StructuredBuffer<Light> gAllLights;


struct RayPayload
{
    float3 hitColor;
    int depth;
    uint rngState;
};

float NextRandomFloat(inout uint state) {
    state = state * 1664525u + 1013904223u;
    return float(state & 0x00FFFFFFu) / float(0x01000000u);
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
        asfloat(asint(p.x) + (p.x < 0 ? float(-of_i.x) : float(of_i.x))),
        asfloat(asint(p.y) + (p.y < 0 ? float(-of_i.y) : float(of_i.y))),
        asfloat(asint(p.z) + (p.z < 0 ? float(-of_i.z) : float(of_i.z)))
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
    // Instance corresponds directly to one Submesh
    uint submeshID = InstanceID();
    uint primitiveID = PrimitiveIndex();

    Submesh submesh = gAllSubmeshes[submeshID];
    Material material = gAllMaterials[submeshID];

    // Triangle indices
    uint baseIndex = submesh.indexOffset + primitiveID * 3;

    uint i0 = gAllIndices[baseIndex + 0];
    uint i1 = gAllIndices[baseIndex + 1];
    uint i2 = gAllIndices[baseIndex + 2];

    // Fetch vertices
    Vertex v0 = gAllVertices[i0];
    Vertex v1 = gAllVertices[i1];
    Vertex v2 = gAllVertices[i2];

    // Barycentrics
    float3 barycentrics = float3(
        1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
        attribs.barycentrics.x,
        attribs.barycentrics.y);

    // Interpolate normal
    float3 localNormal =
        normalize(
            v0.normal.xyz * barycentrics.x +
            v1.normal.xyz * barycentrics.y +
            v2.normal.xyz * barycentrics.z);

    float2 uv = v0.uv.xy * barycentrics.x + 
                 v1.uv.xy * barycentrics.y + 
                 v2.uv.xy * barycentrics.z;

    float3x3 normalMatrix = ((float3x3)WorldToObject3x4());

    float3 N =
        normalize(mul(localNormal, normalMatrix));

    float3 worldPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 V = normalize(-WorldRayDirection());

    float3 albedo = material.albedo.xyz;
    float roughness = material.roughnessMetallic.x;
    float metallic = material.roughnessMetallic.y;
    float3 F0 = lerp(float3(0.04), albedo, metallic);

    float3 emission = material.emissiveColor.xyz; // * material.intensity;

    float3 directColor = float3(0,0,0);

    for(uint i = 0; i < 1; i++) {
        Light light = gAllLights[i];

        if(light.type == LightType::QuadAreaLight) {

            for(uint j = 0; j < 4; j++) {
                uint offsetSeed = payload.rngState + 100 * j;
                float u1 = NextRandomFloat(offsetSeed) * 2.0 - 1.0;
                float u2 = NextRandomFloat(offsetSeed) * 2.0 - 1.0;
                float3 samplePos = light.position + (light.tangentX * u1) + (light.tangentY * u2);

                float3 L = samplePos - worldPos;
                float dist = length(L);
                L = normalize(L);

                float NdotL = max(dot(N, L), 0.0);
                float lightNdotL = max(dot(light.normal, -L), 0.0);

                // If the surface faces the light and the light faces the surface
                if (NdotL > 0.0 && lightNdotL > 0.0) {
                
                    if (true) { //!shadowPayload.isShadowed) {
                        // Evaluate BRDF
                        float3 H = normalize(V + L);
                        float  D = DistributionGGX(N, H, roughness);
                        float  G = GeometrySmith(N, V, L, roughness);
                        float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

                        float3 numerator = D * G * F;
                        float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL;
                        float3 specular = numerator / max(denominator, 0.000001);

                        float3 kS = F;
                        float3 kD = (float3(1.0) - kS) * (1.0 - metallic);

                        // Area light geometric attenuation (solid angle formulation)
                        float geometryFactor = (NdotL * lightNdotL) / (dist * dist + 0.0001);
                        float3 radiance = light.color * light.area * geometryFactor;

                        directColor += (kD * albedo / PI + specular) * radiance;
                    }
                }
            }

            directColor /= 4.0;
        }
    }

    float3 indirectColor = float3(0.0);
    const int MAX_BOUNCES = 4;

    if (payload.depth <= MAX_BOUNCES) {
        float3 L_ind = reflect(-V, N);
        float3 H_ind = normalize(V + L_ind);
        float NdotL_ind = max(dot(N, L_ind), 0.0);

        if (NdotL_ind > 0.0) {
            RayPayload secondaryPayload;
            secondaryPayload.hitColor = float3(0.0);
            secondaryPayload.depth = payload.depth + 1;
            secondaryPayload.rngState = payload.rngState; 

            float3 rayOrigin = SafeOffsetRayOrigin(worldPos, N);

            RayDesc ray;
            ray.Origin    = rayOrigin;
            ray.Direction = L_ind;
            ray.TMin      = 0.001f;
            ray.TMax      = 10000.0f;

            TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, secondaryPayload);

            float  D_ind = DistributionGGX(N, H_ind, roughness);
            float  G_ind = GeometrySmith(N, V, L_ind, roughness);
            float3 F_ind = FresnelSchlick(max(dot(H_ind, V), 0.0), F0);

            float3 numerator_ind = D_ind * G_ind * F_ind;
            float denominator_ind = 4.0 * max(dot(N, V), 0.0) * NdotL_ind;
            float3 specular_ind = numerator_ind / max(denominator_ind, 0.000001);

            float3 kS_ind = F_ind;
            float3 kD_ind = (float3(1.0) - kS_ind) * (1.0 - metallic);

            indirectColor = (kD_ind * albedo / PI + specular_ind) * secondaryPayload.hitColor * NdotL_ind;
        }
    }

    payload.hitColor = emission + directColor + indirectColor;



}