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
    uint materialIndex;
    uint pa0;
    uint pad1;
    uint pad2;
};

struct Material {
    float4 albedo;                         // Offset: 0   (16 bytes)
    float2 roughnessMetallic;              // Offset: 16  (8 bytes)
    int albedoTextureIndex;               // Offset: 24  (4 bytes)
    int roughnessMetallicTextureIndex;    // Offset: 28  (4 bytes)
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

[[vk::binding(1, 1)]] Sampler2D gTextures[];

struct RayPayload
{
    float3 hitColor;
    int depth;
    uint rngState;
};

struct ShadowPayload {
    bool isShadowed;
};

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

bool IsShadowed(float3 position, float3 normal, float3 lightDirection, float distance) {
    RayDesc shadowRayDesc;
    shadowRayDesc.Origin = position + 0.001f * normal;
    shadowRayDesc.Direction = lightDirection;
    shadowRayDesc.TMin = 0.001f;
    shadowRayDesc.TMax = distance - 0.002f;

    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

    query.TraceRayInline(tlas, rayFlags, 0xFF, shadowRayDesc);

    query.Proceed();
                
    bool isShadowed = (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT);

    return isShadowed;
}

[shader("closesthit")]
void main(
    inout RayPayload payload,
    BuiltInTriangleIntersectionAttributes attribs)
{
    uint submeshID = InstanceID();
    uint primitiveID = PrimitiveIndex();

    Submesh submesh = gAllSubmeshes[submeshID];
    Material material = gAllMaterials[submesh.materialIndex];

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

    if(material.albedoTextureIndex >= 0) {
        albedo = gTextures[material.albedoTextureIndex].SampleLevel(uv, 0.0f).xyz;
    }

    float roughness = max(material.roughnessMetallic.x, 0.0f);
    float metallic = material.roughnessMetallic.y;

    if(material.roughnessMetallicTextureIndex >= 0) {
        float3 values = gTextures[material.roughnessMetallicTextureIndex].SampleLevel(uv, 0.0f).rgb;
        roughness = values.g;
        metallic = values.b;
    }

    float3 V = normalize(-WorldRayDirection());

    float3 F0 = lerp(float3(0.04f), albedo, metallic);
    float3 emission = material.emissiveColor.xyz; 
    float3 directColor = float3(0.0);

    uint seed = payload.rngState;

    for(uint i = 0; i < 2; i++) {
        Light light = gAllLights[i];

        if(light.type == (int)LightType::Directional) {
            float3 L = normalize(light.normal);
            float NdotL = max(dot(N, L), 0.0f);

            if(!IsShadowed(worldPos, N, L, 10000.0f)) {
                // 1. Evaluate standard BRDF terms f(l, v)
                float3 H = normalize(V + L);
                float  D = DistributionGGX(N, H, roughness);
                float  G = GeometrySmith(N, V, L, roughness);
                float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);

                float3 numerator = D * G * F;
                // Standard BRDF denominator includes NdotV and NdotL
                float denominator = 4.0f * max(dot(N, V), 0.0f) * NdotL;
                float3 specular = numerator / max(denominator, 0.000001f);

                float3 kS = F;
                float3 kD = (float3(1.0f) - kS) * (1.0f - metallic);
        
                float3 BRDF = (kD * albedo / PI) + specular;
        
                float3 emmitedRadiance = light.color; 

                directColor += BRDF * emmitedRadiance * NdotL;
            }
            
        
        } else if(light.type == (int)LightType::QuadAreaLight) {

            for(uint j = 0; j < 4; j++) {
                // Fix 3: Keep updating the seed state sequentially so sample positions are random
                float u1 = NextRandomFloat(seed) * 2.0f - 1.0f;
                float u2 = NextRandomFloat(seed) * 2.0f - 1.0f;
                float3 samplePos = light.position + (light.tangentX * u1) + (light.tangentY * u2);

                float3 L = samplePos - worldPos;
                float dist = length(L);
                L = normalize(L);

                float lightNdotL = max(dot(light.normal, -L), 0.0f);
                float NdotL = max(dot(N, L), 0.0f);

                if (NdotL > 0.0f && lightNdotL > 0.0f) {
                    if(!IsShadowed(worldPos, N, L, dist)) {
        
                        // 1. Evaluate standard BRDF terms f(l, v)
                        float3 H = normalize(V + L);
                        float  D = DistributionGGX(N, H, roughness);
                        float  G = GeometrySmith(N, V, L, roughness);
                        float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);

                        float3 numerator = D * G * F;
                        // Standard BRDF denominator includes NdotV and NdotL
                        float denominator = 4.0f * max(dot(N, V), 0.0f) * NdotL;
                        float3 specular = numerator / max(denominator, 0.000001f);

                        float3 kS = F;
                        float3 kD = (float3(1.0f) - kS) * (1.0f - metallic);
        
                        float3 BRDF = (kD * albedo / PI) + specular;

                        // 2. Correct Monte Carlo Weighting for Area Sampling
                        // Solid Angle conversion factor: (lightNdotL) / (dist * dist)
                        float solidAngleConversion = lightNdotL / (dist * dist + 0.0001f);
        
                        // Target Radiance from the emitter
                        float3 emmitedRadiance = light.color; 

                        // 3. Final Integration: BRDF * Radiance * NdotL / PDF
                        // Because PDF = 1.0 / light.area, dividing by PDF means multiplying by light.area
                        directColor += BRDF * emmitedRadiance * NdotL * solidAngleConversion * light.area;
                    }
                }
              

                
            }
            directColor /= 4.0f;
        }
    }
    
    // Pass updated seed back to payload
    payload.rngState = seed;



    // INDIRECT LIGHT
    float3 indirectColor = float3(0.0f, 0.0f, 0.0f);
    const int MAX_BOUNCES = 4;

    if (payload.depth <= MAX_BOUNCES) {
        uint seed = payload.rngState;
        float r1 = NextRandomFloat(seed);
        float r2 = NextRandomFloat(seed);
        payload.rngState = seed; 

        float3 F_ind = FresnelSchlick(max(dot(N, V), 0.0f), F0);
        float specularChance = max(max(F_ind.x, F_ind.y), F_ind.z);
        
        float3 L_ind;
        bool isSpecularBounce = false;

        if (NextRandomFloat(seed) < specularChance) {
            // 1. SPECULAR BOUNCE (Mirror Direction)
            L_ind = reflect(-V, N);
            isSpecularBounce = true;
        } else {
            // 2. DIFFUSE BOUNCE (Proper Cosine-Weighted Hemisphere Sampling)
            float phi = 2.0f * PI * r1;
            float cosTheta = sqrt(r2);
            float sinTheta = sqrt(1.0f - r2);

            // Construct local space coordinates
            float3 localDir = float3(
                cos(phi) * sinTheta,
                sin(phi) * sinTheta,
                cosTheta
            );

            // Build a stable coordinate system around the Normal vector
            float3 up = abs(N.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
            float3 tangent = normalize(cross(up, N));
            float3 bitangent = cross(N, tangent);

            // Transform local hemisphere sample back to world space
            L_ind = tangent * localDir.x + bitangent * localDir.y + N * localDir.z;
            L_ind = normalize(L_ind);
        }
        
        payload.rngState = seed;

        float NdotL_ind = max(dot(N, L_ind), 0.0f);
         
        if (NdotL_ind > 0.0f) {
            RayPayload secondaryPayload;
            secondaryPayload.hitColor = float3(0.0f, 0.0f, 0.0f);
            secondaryPayload.depth = payload.depth + 1;
            secondaryPayload.rngState = payload.rngState; 

            float3 rayOrigin = worldPos + 0.001 * N;

            RayDesc ray;
            ray.Origin    = rayOrigin;
            ray.Direction = L_ind;
            ray.TMin      = 0.001f;
            ray.TMax      = 10000.0f;

            TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, secondaryPayload);
            
            payload.rngState = secondaryPayload.rngState;

            if (isSpecularBounce) {
                // PDF for picking specular is specularChance
                indirectColor = secondaryPayload.hitColor * F_ind / max(specularChance, 0.001f);
            } else {
                // PDF for picking diffuse is (1.0 - specularChance)
                float3 kD_ind = (float3(1.0f) - F_ind) * (1.0f - metallic);
                // Note: The cosine weight (NdotL_ind) cancels out perfectly with the cosine PDF allocation!
                indirectColor = secondaryPayload.hitColor * albedo * kD_ind / max(1.0f - specularChance, 0.001f);
            }
        }
               
    }

    payload.hitColor = emission + directColor + indirectColor;

}