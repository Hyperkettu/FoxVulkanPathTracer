// Closest hit: looks up the geometry and material at the hit point and hands a description of
// the surface back to the path tracer (raygen.slang). No lighting is computed here.

#include "common.slang"
#include "terrain.slang"
#include "water_waves.slang"

[[vk::binding(2, 0)]] ConstantBuffer<Camera> camera;

// Where the vertices of skinned meshes were in the previous frame, mesh space (see skinning.slang)
[[vk::binding(15, 0)]] StructuredBuffer<float4> gPreviousSkinnedPositions;

[shader("closesthit")]
void main(inout HitPayload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    // Each submesh is one geometry of its acceleration structure, and the instance's custom index is the
    // index of its first submesh (see RayTracingScene::UploadGeometry)
    Submesh submesh = gAllSubmeshes[InstanceID() + GeometryIndex()];
    Material material = gAllMaterials[submesh.materialIndex];

    uint baseIndex = submesh.indexOffset + PrimitiveIndex() * 3;

    uint3 vertexIndices = uint3(gAllIndices[baseIndex + 0], gAllIndices[baseIndex + 1], gAllIndices[baseIndex + 2]);
    Vertex v0 = gAllVertices[vertexIndices.x];
    Vertex v1 = gAllVertices[vertexIndices.y];
    Vertex v2 = gAllVertices[vertexIndices.z];

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

    float3 baseColor;
    float roughness;
    float metallic;
    float occlusion;
    float3 emission;

    if (material.layerMaterial >= 0)
    {
        // --- Terrain: blended layers, projected from the position (see terrain.slang) ---
        float3 localPosition = v0.position.xyz * barycentrics.x + v1.position.xyz * barycentrics.y + v2.position.xyz * barycentrics.z;
        float3 smoothNormal = dot(localNormal, localNormal) > 0.0f ? normalize(localNormal) : float3(0.0f, 1.0f, 0.0f);
        float2 blendUV = v0.uv * barycentrics.x + v1.uv * barycentrics.y + v2.uv * barycentrics.z;
        TerrainSurface terrain = EvaluateTerrain(material, localPosition, smoothNormal, blendUV);

        N = normalize(mul(terrain.normal, worldToObject));
        baseColor = terrain.baseColor;
        roughness = terrain.roughness;
        metallic = terrain.metallic;
        occlusion = terrain.occlusion;
        emission = float3(0.0f);
    }
    else
    {
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
        baseColor = material.baseColor.rgb;
        if (material.baseColorTexture >= 0)
        {
            baseColor *= SampleTexture(material.baseColorTexture, uv).rgb;
        }

        roughness = material.roughness;
        metallic = material.metallic;
        if (material.metallicRoughnessTexture >= 0)
        {
            // glTF packing: G = roughness, B = metallic
            float3 roughnessMetallic = SampleTexture(material.metallicRoughnessTexture, uv).rgb;
            roughness *= roughnessMetallic.g;
            metallic *= roughnessMetallic.b;
        }

        // Ambient occlusion: only for light from the surroundings (applied in raygen.slang)
        occlusion = 1.0f;
        if (material.occlusionTexture >= 0)
        {
            occlusion = lerp(1.0f, SampleTexture(material.occlusionTexture, uv).r, saturate(material.occlusionStrength));
        }

        emission = material.emissive;
        if (material.emissiveTexture >= 0)
        {
            emission *= SampleTexture(material.emissiveTexture, uv).rgb;
        }

        // Particles: each its own brightness and heat
        if (material.layerMaterial == PARTICLE_MATERIAL)
        {
            float2 glow = v0.padding * barycentrics.x + v1.padding * barycentrics.y + v2.padding * barycentrics.z;
            if (glow.y >= 0.0f)
            {
                emission *= FireColor(glow.y);
            }
            else
            {
                // Between its two colours (orbs: each its own shade)
                float3 second = material.baseColor.rgb * MaxComponent(material.emissive);
                if (material.emissiveTexture >= 0)
                    second *= SampleTexture(material.emissiveTexture, uv).rgb;
                emission = lerp(emission, second, saturate(-1.0f - glow.y));
            }
            emission *= max(glow.x, 0.0f);
        }
    }

    // --- Water: the wind's ripples, finer than its simulation, and foam ---
    float transmission = material.transmission;
    if (material.layerMaterial == WATER_MATERIAL)
    {
        float3 position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
        float footprint = length(position - camera.camPos) * 2.0f * camera.tanHalfFov / max(camera.resolution.y, 1.0f);
        float2 wind = float2(material.uvScale[0], material.uvScale[1]);
        N = WaterRipples(N, position, wind, material.normalScale, camera.time, footprint, roughness);

        // Foam is a froth of bubbles: it scatters light every way, so it is white, rough and opaque
        float foamAmount = v0.padding.x * barycentrics.x + v1.padding.x * barycentrics.y + v2.padding.x * barycentrics.z;
        float coverage = WaterFoamCoverage(foamAmount, position, wind, camera.time, footprint);
        baseColor = lerp(baseColor, float3(0.92f, 0.93f, 0.94f), coverage);
        roughness = lerp(roughness, 0.85f, coverage);
        transmission *= 1.0f - coverage;
    }

    payload.hitT = RayTCurrent();
    payload.geometricNormal = geometricNormal;
    payload.shadingNormal = N;
    payload.baseColor = baseColor;
    payload.metallic = metallic;
    payload.roughness = roughness;
    payload.transmission = transmission;
    payload.ior = material.ior;
    payload.absorption = float3(material.absorptionR, material.absorptionG, material.absorptionB);
    payload.specularWeight = material.specularWeight;
    payload.emission = emission;
    // Emissive triangles are lights each, in order from the submesh's first (pad2 - 1). The index is that of the
    // mesh's first object; raygen adds the offset of this instance's object.
    payload.lightIndex = submesh.pad2 > 0 ? int(submesh.pad2 - 1 + PrimitiveIndex()) : material.lightIndex;
    payload.instanceIndex = InstanceIndex();
    payload.occlusion = occlusion;

    // Skinned surfaces move by themselves: how far this point moved since the previous frame
    payload.skinnedMotion = float3(0.0f);
    if (submesh.previousPositions > 0)
    {
        uint3 previous = (submesh.previousPositions - 1) + (vertexIndices - submesh.vertexOffset);
        float3 previousPosition = gPreviousSkinnedPositions[previous.x].xyz * barycentrics.x
                                + gPreviousSkinnedPositions[previous.y].xyz * barycentrics.y
                                + gPreviousSkinnedPositions[previous.z].xyz * barycentrics.z;
        float3 currentPosition = v0.position.xyz * barycentrics.x + v1.position.xyz * barycentrics.y + v2.position.xyz * barycentrics.z;
        payload.skinnedMotion = mul(objectToWorld, previousPosition - currentPosition);
    }
}
