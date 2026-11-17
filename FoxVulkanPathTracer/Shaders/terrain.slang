// Terrain surfaces (Scene/Terrain, MainRayTracingScene::PlaceTerrain): TERRAIN_LAYERS ground types blended by a
// blend map, each layer's textures projected along the three axes (triplanar), so that steep slopes do not
// stretch them the way a single top-down projection would.
//
// The terrain's material: baseColorTexture = blend map (RGBA = weights of layers 0..3, over the whole terrain by
// the vertex uv), layerMaterial = the first layer material, occlusionStrength = blend softness.
// A layer material: baseColor = tint, baseColorTexture = albedo, normalTexture + normalScale, metallicRoughnessTexture
// = packed occlusion (R) / roughness (G) / metallic (B) with the roughness and metallic factors, occlusionTexture
// (the same packed texture, or -1), emissiveTexture = height map (R), uvScale.x = texture repeats per meter,
// uvRotation = 1 for a DirectX normal map (green down).

#ifndef FOX_TERRAIN_SLANG
#define FOX_TERRAIN_SLANG

#include "common.slang"

static const int TERRAIN_LAYERS = 4;

struct TerrainSurface
{
    float3 baseColor;
    float3 normal;     // Terrain (object) space
    float roughness;
    float metallic;
    float occlusion;
};

// Projection weights: mostly the axis the surface faces, sharpened so the blend zone on slopes stays narrow
float3 TriplanarWeights(float3 n)
{
    float3 w = pow(abs(n), 4.0f);
    w /= (w.x + w.y + w.z);
    // Projections that barely contribute are skipped (saves their texture reads)
    w = select(w < 0.02f, 0.0f, w);
    return w / (w.x + w.y + w.z);
}

// A texture sampled with the three projections and blended
float4 SampleTriplanar(int texture, float2 uvX, float2 uvY, float2 uvZ, float3 w, float4 fallback)
{
    if (texture < 0)
        return fallback;
    float4 result = 0.0f;
    if (w.x > 0.0f) result += SampleTexture(texture, uvX) * w.x;
    if (w.y > 0.0f) result += SampleTexture(texture, uvY) * w.y;
    if (w.z > 0.0f) result += SampleTexture(texture, uvZ) * w.z;
    return result;
}

float3 UnpackLayerNormal(int texture, float2 uv, Material layer)
{
    float3 t = SampleTexture(texture, uv).xyz * 2.0f - 1.0f;
    if (layer.uvRotation > 0.5f)
        t.y = -t.y; // DirectX convention
    t.xy *= layer.normalScale;
    return t;
}

struct LayerSample
{
    float3 albedo;
    float3 normal;
    float roughness;
    float metallic;
    float occlusion;
    float height;
};

LayerSample SampleLayer(Material layer, float3 position, float3 n, float3 w)
{
    // Each projection is mirrored on the negative side of its axis, so the textures read the right way round
    float3 axisSign = select(n < 0.0f, -1.0f, 1.0f);
    float3 p = position * layer.uvScale.x;
    float2 uvX = float2(p.z * axisSign.x, p.y);
    float2 uvY = float2(p.x * axisSign.y, p.z);
    float2 uvZ = float2(-p.x * axisSign.z, p.y);

    LayerSample s;
    s.albedo = layer.baseColor.rgb * SampleTriplanar(layer.baseColorTexture, uvX, uvY, uvZ, w, float4(1.0f)).rgb;

    float3 orm = SampleTriplanar(layer.metallicRoughnessTexture, uvX, uvY, uvZ, w, float4(1.0f, 1.0f, 0.0f, 1.0f)).rgb;
    s.roughness = saturate(layer.roughness * orm.g);
    s.metallic = saturate(layer.metallic * orm.b);
    s.occlusion = layer.occlusionTexture >= 0 ? lerp(1.0f, orm.r, saturate(layer.occlusionStrength)) : 1.0f;
    s.height = SampleTriplanar(layer.emissiveTexture, uvX, uvY, uvZ, w, float4(0.5f)).r;

    // Normal maps per projection, combined with the surface normal ("whiteout" blend, after Ben Golus)
    s.normal = n;
    if (layer.normalTexture >= 0)
    {
        float3 absN = abs(n);
        float3 sum = 0.0f;
        if (w.x > 0.0f)
        {
            float3 t = UnpackLayerNormal(layer.normalTexture, uvX, layer);
            t.x *= axisSign.x;
            t = float3(t.xy + n.zy, absN.x * t.z);
            t.z *= axisSign.x;
            sum += t.zyx * w.x;
        }
        if (w.y > 0.0f)
        {
            float3 t = UnpackLayerNormal(layer.normalTexture, uvY, layer);
            t.x *= axisSign.y;
            t = float3(t.xy + n.xz, absN.y * t.z);
            t.z *= axisSign.y;
            sum += t.xzy * w.y;
        }
        if (w.z > 0.0f)
        {
            float3 t = UnpackLayerNormal(layer.normalTexture, uvZ, layer);
            t.x *= -axisSign.z;
            t = float3(t.xy + n.xy, absN.z * t.z);
            t.z *= axisSign.z;
            sum += t.xyz * w.z;
        }
        if (dot(sum, sum) > 1e-12f)
            s.normal = normalize(sum);
    }
    return s;
}

// position, normal: terrain space, interpolated; blendUV: the vertex uv
TerrainSurface EvaluateTerrain(Material terrain, float3 position, float3 normal, float2 blendUV)
{
    float4 weights = SampleTexture(terrain.baseColorTexture, blendUV);
    weights /= max(weights.x + weights.y + weights.z + weights.w, 1e-4f);

    float3 w = TriplanarWeights(normal);

    // Each layer present here, then blended by height: where two layers mix, the one whose height map is
    // higher at a point shows there (stones poke out of grass) instead of a uniform crossfade
    LayerSample samples[TERRAIN_LAYERS];
    float levels[TERRAIN_LAYERS];
    float top = -1e9f;
    [unroll]
    for (int i = 0; i < TERRAIN_LAYERS; i++)
    {
        levels[i] = -1e9f;
        if (weights[i] > 0.004f)
        {
            samples[i] = SampleLayer(gAllMaterials[terrain.layerMaterial + i], position, normal, w);
            levels[i] = samples[i].height + weights[i];
            top = max(top, levels[i]);
        }
        else
        {
            samples[i] = (LayerSample)0;
        }
    }

    float softness = max(terrain.occlusionStrength, 0.001f);
    TerrainSurface result = (TerrainSurface)0;
    float total = 0.0f;
    float3 blendedNormal = 0.0f;
    [unroll]
    for (int i = 0; i < TERRAIN_LAYERS; i++)
    {
        float b = max(levels[i] - (top - softness), 0.0f);
        if (b <= 0.0f)
            continue;
        result.baseColor += samples[i].albedo * b;
        blendedNormal += samples[i].normal * b;
        result.roughness += samples[i].roughness * b;
        result.metallic += samples[i].metallic * b;
        result.occlusion += samples[i].occlusion * b;
        total += b;
    }

    if (total <= 0.0f)
    {
        result.baseColor = float3(0.5f);
        result.normal = normal;
        result.roughness = 1.0f;
        result.occlusion = 1.0f;
        return result;
    }
    result.baseColor /= total;
    result.roughness /= total;
    result.metallic /= total;
    result.occlusion /= total;
    result.normal = dot(blendedNormal, blendedNormal) > 1e-12f ? normalize(blendedNormal) : normal;
    return result;
}

#endif
