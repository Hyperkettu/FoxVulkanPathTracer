// Closest hit: looks up the geometry and material at the hit point and hands a description of
// the surface back to the path tracer (raygen.slang). No lighting is computed here.

#include "common.slang"

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

[shader("closesthit")]
void main(inout HitPayload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    // Each submesh is one geometry of its acceleration structure, and the instance's custom index is the
    // index of its first submesh (see RayTracingScene::UploadGeometry)
    Submesh submesh = gAllSubmeshes[InstanceID() + GeometryIndex()];
    Material material = gAllMaterials[submesh.materialIndex];

    uint baseIndex = submesh.indexOffset + PrimitiveIndex() * 3;

    Vertex v0 = gAllVertices[gAllIndices[baseIndex + 0]];
    Vertex v1 = gAllVertices[gAllIndices[baseIndex + 1]];
    Vertex v2 = gAllVertices[gAllIndices[baseIndex + 2]];

    float3 barycentrics = float3(
        1.0f - attribs.barycentrics.x - attribs.barycentrics.y,
        attribs.barycentrics.x,
        attribs.barycentrics.y);

    float3x3 objectToWorld = (float3x3)ObjectToWorld3x4();
    float3x3 worldToObject = (float3x3)WorldToObject3x4();

    // --- Geometry ---
    float3 edge1 = v1.position.xyz - v0.position.xyz;
    float3 edge2 = v2.position.xyz - v0.position.xyz;

    // Normals transform with the inverse transpose, which keeps them perpendicular under non-uniform scale
    float3 geometricNormal = normalize(mul(cross(edge1, edge2), worldToObject));

    float3 localNormal = v0.normal.xyz * barycentrics.x + v1.normal.xyz * barycentrics.y + v2.normal.xyz * barycentrics.z;
    float3 N = dot(localNormal, localNormal) > 0.0f ? normalize(mul(localNormal, worldToObject)) : geometricNormal;

    // The geometric normal faces the same way as the authored normals (outwards on closed meshes),
    // whatever the triangle winding: refraction relies on it to tell entering from leaving a solid
    if (dot(geometricNormal, N) < 0.0f)
        geometricNormal = -geometricNormal;

    float2 uv0 = TransformUV(v0.uv, material);
    float2 uv1 = TransformUV(v1.uv, material);
    float2 uv2 = TransformUV(v2.uv, material);
    float2 uv = uv0 * barycentrics.x + uv1 * barycentrics.y + uv2 * barycentrics.z;

    // --- Normal mapping ---
    if (material.normalTexture >= 0)
    {
        float4 localTangent = v0.tangent * barycentrics.x + v1.tangent * barycentrics.y + v2.tangent * barycentrics.z;

        float3 tangent;
        float handedness;

        if (dot(localTangent.xyz, localTangent.xyz) > 1e-8f)
        {
            tangent = mul(objectToWorld, localTangent.xyz);
            handedness = localTangent.w < 0.0f ? -1.0f : 1.0f;
        }
        else
        {
            // No tangents in the mesh: derive the direction of increasing U from the triangle itself
            float2 duv1 = uv1 - uv0;
            float2 duv2 = uv2 - uv0;
            float det = duv1.x * duv2.y - duv2.x * duv1.y;

            tangent = mul(objectToWorld, (edge1 * duv2.y - edge2 * duv1.y) * (abs(det) > 1e-12f ? 1.0f / det : 1.0f));
            handedness = det < 0.0f ? -1.0f : 1.0f;
        }

        // Gram-Schmidt: make the tangent perpendicular to the normal
        tangent = tangent - N * dot(N, tangent);

        if (dot(tangent, tangent) > 1e-12f)
        {
            tangent = normalize(tangent);
            float3 bitangent = cross(N, tangent) * handedness;

            float3 mapped = SampleTexture(material.normalTexture, uv).xyz * 2.0f - 1.0f;
            mapped.xy *= material.normalScale;

            N = normalize(tangent * mapped.x + bitangent * mapped.y + N * mapped.z);
        }
    }

    // --- Material ---
    float3 baseColor = material.baseColor.rgb;
    if (material.baseColorTexture >= 0)
    {
        baseColor *= SampleTexture(material.baseColorTexture, uv).rgb;
    }

    float roughness = material.roughness;
    float metallic = material.metallic;
    if (material.metallicRoughnessTexture >= 0)
    {
        // glTF packing: G = roughness, B = metallic
        float3 roughnessMetallic = SampleTexture(material.metallicRoughnessTexture, uv).rgb;
        roughness *= roughnessMetallic.g;
        metallic *= roughnessMetallic.b;
    }

    float3 emission = material.emissive;
    if (material.emissiveTexture >= 0)
    {
        emission *= SampleTexture(material.emissiveTexture, uv).rgb;
    }

    payload.hitT = RayTCurrent();
    payload.geometricNormal = geometricNormal;
    payload.shadingNormal = N;
    payload.baseColor = baseColor;
    payload.metallic = metallic;
    payload.roughness = roughness;
    payload.transmission = material.transmission;
    payload.ior = material.ior;
    payload.absorption = float3(material.absorptionR, material.absorptionG, material.absorptionB);
    payload.specularWeight = material.specularWeight;
    payload.emission = emission;
    payload.lightIndex = material.lightIndex;
    payload.instanceIndex = InstanceIndex();
}
