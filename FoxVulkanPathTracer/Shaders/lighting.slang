// Light sources of the path tracer and how to sample them:
//   - the HDR environment map, importance sampled so the sun and bright sky are found reliably
//   - lights collected from the scene: the sun and lamps, emissive quads, and every triangle of the other emissive
//     surfaces (their emissive maps included), picked by how much light they give off
//
// Used by raygen.slang only.

#ifndef FOX_LIGHTING_SLANG
#define FOX_LIGHTING_SLANG

#include "common.slang"

// ------------------------------------------------------------------------------------------------
// Environment map
// ------------------------------------------------------------------------------------------------

// Mirrors Fox::Graphics::Vulkan::Environment (ConstantBuffers.h).
struct Environment
{
    uint environmentMapTextureIndex;
    float environmentMapIntensity;
    uint tableWidth;   // Resolution of the importance sampling table
    uint tableHeight;
};

// One cell of the importance sampling table, an alias table over a low resolution copy of the
// map (see TextureManager::BuildEnvironmentSamplingTable). Mirrors Fox::Graphics::Vulkan::EnvironmentCell.
struct EnvironmentCell
{
    float threshold;   // Probability of keeping this cell instead of jumping to `alias`
    uint alias;
    float probability; // Chance that sampling ends up in this cell
    float pad;
};

[[vk::binding(0, 1)]] ConstantBuffer<Environment> environment;
[[vk::binding(12, 0)]] StructuredBuffer<EnvironmentCell> gEnvironmentTable;

// Equirectangular mapping: +Y is the top row of the map.
float2 DirectionToEnvironmentUV(float3 direction)
{
    float u = atan2(direction.z, direction.x) / (2.0f * PI) + 0.5f;
    float v = acos(clamp(direction.y, -1.0f, 1.0f)) / PI;
    return float2(u, v);
}

float3 EnvironmentUVToDirection(float2 uv)
{
    float phi = (uv.x - 0.5f) * 2.0f * PI;
    float theta = uv.y * PI;
    float sinTheta = sin(theta);
    return float3(sinTheta * cos(phi), cos(theta), sinTheta * sin(phi));
}

float3 EnvironmentRadiance(float3 direction)
{
    float2 uv = DirectionToEnvironmentUV(direction);
    float3 radiance = gTextures[NonUniformResourceIndex(environment.environmentMapTextureIndex)].SampleLevel(uv, 0.0f).rgb;
    return radiance * environment.environmentMapIntensity;
}

// Converts the probability of a table cell into a density over solid angle
float EnvironmentCellPdf(float probability, float v)
{
    // A cell covers (2 pi / width) * (pi / height) * sin(theta) steradians
    float sinTheta = max(sin(v * PI), 1e-4f);
    float cellCount = float(environment.tableWidth * environment.tableHeight);
    return probability * cellCount / (2.0f * PI * PI * sinTheta);
}

// Density with which SampleEnvironment() picks the given direction
float EnvironmentPdf(float3 direction)
{
    float2 uv = DirectionToEnvironmentUV(direction);
    uint x = min(uint(uv.x * float(environment.tableWidth)), environment.tableWidth - 1);
    uint y = min(uint(uv.y * float(environment.tableHeight)), environment.tableHeight - 1);

    return EnvironmentCellPdf(gEnvironmentTable[y * environment.tableWidth + x].probability, uv.y);
}

// Picks a direction proportionally to the brightness of the environment
float3 SampleEnvironment(inout uint rng, out float pdf)
{
    uint cellCount = environment.tableWidth * environment.tableHeight;

    // Alias method: pick a cell uniformly, then either keep it or take its alias
    uint index = min(uint(NextRandomFloat(rng) * float(cellCount)), cellCount - 1);
    EnvironmentCell cell = gEnvironmentTable[index];

    if (NextRandomFloat(rng) >= cell.threshold)
    {
        index = cell.alias;
        cell = gEnvironmentTable[index];
    }

    // Uniform position inside the cell
    float2 jitter = NextRandomFloat2(rng);
    float2 uv = float2(
        (float(index % environment.tableWidth) + jitter.x) / float(environment.tableWidth),
        (float(index / environment.tableWidth) + jitter.y) / float(environment.tableHeight));

    pdf = EnvironmentCellPdf(cell.probability, uv.y);
    return EnvironmentUVToDirection(uv);
}

// ------------------------------------------------------------------------------------------------
// Analytic lights
// ------------------------------------------------------------------------------------------------

// Mirrors Fox::Core::Loaders::GLTF::LightType
enum class LightType
{
    Directional = 0,
    Point,
    Spot,
    QuadAreaLight,
    EmissiveTriangle
};

// Mirrors Fox::Graphics::Vulkan::Light (Light.h)
struct Light
{
    float3 position;   // Quad: center. Point / spot: where it is. Triangle: its first corner
    float  _pad0;
    float3 normal;     // Quad, triangle: surface normal. Directional: direction towards the light. Spot: direction it shines
    float  _pad1;
    float3 tangentX;   // Quad: half extent along its first edge. Spot: x = cos(outer cone angle), y = cos(inner). Triangle: first edge
    float  _pad2;
    float3 tangentY;   // Quad: half extent along its second edge. Triangle: second edge
    float  _pad3;
    float3 color;      // Quad, triangle: emitted radiance. Directional: irradiance on a surface facing the light. Point / spot: intensity
    float  _pad4;
    float  area;       // Quad, triangle: surface area. Directional: angular radius of the disc in radians. Point / spot: bulb radius
    int    type;
    int    material;   // Triangle: its material
    float  selection;  // Chance that SampleLight picks it
    float  cumulative; // Sum of the chances up to and including it
    float  u0;         // Triangle: texture coordinates of its corners
    float  v0;
    float  u1;
    float  v1;
    float  u2;
    float  v2;
    float  _pad5;
};

[[vk::binding(7, 0)]] StructuredBuffer<Light> gAllLights;

struct LightSample
{
    float3 L;         // Direction towards the light
    float distance;
    float3 radiance;
    float pdf;        // Density over solid angle, including the choice of the light
    bool isDelta;     // Cannot be hit by a randomly chosen direction (directional light)
    bool valid;
};

// Density with which SampleLight() would generate the direction towards a point on an area light (quad or
// triangle), given the distance to that point and the cosine at the light's surface.
float AreaLightPdf(int lightIndex, float distance, float cosAtLight)
{
    Light light = gAllLights[lightIndex];
    float area = max(light.area, 1e-8f);
    return light.selection * distance * distance / (max(cosAtLight, 1e-6f) * area);
}

// What an emissive triangle gives off at a point of it (barycentrics b1, b2 of its second and third corner)
float3 TriangleEmission(Light light, float b1, float b2)
{
    if (light.material < 0)
        return light.color;
    Material material = gAllMaterials[light.material];
    float3 emission = material.emissive;
    if (material.emissiveTexture >= 0)
    {
        float2 uv = float2(light.u0, light.v0) * (1.0f - b1 - b2) + float2(light.u1, light.v1) * b1 + float2(light.u2, light.v2) * b2;
        emission *= SampleTexture(material.emissiveTexture, TransformUV(uv, material)).rgb;
    }
    return emission;
}

// Picks one light, in proportion to its chance (power for the area lights), and a point on it
LightSample SampleLight(float3 position, uint lightCount, inout uint rng)
{
    LightSample result;
    result.L = float3(0.0f);
    result.distance = 0.0f;
    result.radiance = float3(0.0f);
    result.pdf = 0.0f;
    result.isDelta = false;
    result.valid = false;

    if (lightCount == 0)
        return result;

    // The first light whose running sum passes a random number
    float u = NextRandomFloat(rng);
    uint low = 0, high = lightCount - 1;
    while (low < high)
    {
        uint middle = (low + high) / 2;
        if (gAllLights[middle].cumulative <= u)
            low = middle + 1;
        else
            high = middle;
    }
    uint index = low;
    Light light = gAllLights[index];
    if (light.selection <= 0.0f)
        return result; // Every light is off

    if (light.type == (int)LightType::Directional)
    {
        // Pick a direction inside the disc of the sun: this is what gives shadows a soft edge
        float2 u = NextRandomFloat2(rng);
        float cosTheta = 1.0f - u.x * (1.0f - cos(light.area));
        float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
        float phi = 2.0f * PI * u.y;

        float3 toSun = normalize(light.normal);
        float3 tangent, bitangent;
        BuildBasis(toSun, tangent, bitangent);

        result.L = normalize(tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta) + toSun * cosTheta);
        result.distance = 1e5f;
        result.radiance = light.color;
        result.pdf = light.selection;
        result.isDelta = true;
        result.valid = true;
    }
    else if (light.type == (int)LightType::EmissiveTriangle)
    {
        // A uniformly distributed point of the triangle
        float2 u = NextRandomFloat2(rng);
        float root = sqrt(u.x);
        float b1 = root * (1.0f - u.y);
        float b2 = root * u.y;
        float3 samplePoint = light.position + light.tangentX * b1 + light.tangentY * b2;

        float3 toLight = samplePoint - position;
        float distance = length(toLight);
        float3 L = toLight / max(distance, 1e-6f);

        // Emissive surfaces emit from both sides
        float cosAtLight = abs(dot(light.normal, L));
        if (cosAtLight > 1e-4f && distance > 1e-4f)
        {
            result.L = L;
            result.distance = distance;
            result.radiance = TriangleEmission(light, b1, b2);
            result.pdf = AreaLightPdf(int(index), distance, cosAtLight);
            result.valid = MaxComponent(result.radiance) > 0.0f;
        }
    }
    else if (light.type == (int)LightType::QuadAreaLight)
    {
        float2 u = NextRandomFloat2(rng) * 2.0f - 1.0f;
        float3 samplePoint = light.position + light.tangentX * u.x + light.tangentY * u.y;

        float3 toLight = samplePoint - position;
        float distance = length(toLight);
        float3 L = toLight / max(distance, 1e-6f);

        // Emissive surfaces emit from both sides
        float cosAtLight = abs(dot(light.normal, L));

        if (cosAtLight > 1e-4f && distance > 1e-4f)
        {
            result.L = L;
            result.distance = distance;
            result.radiance = light.color;
            result.pdf = AreaLightPdf(int(index), distance, cosAtLight);
            result.valid = true;
        }
    }
    else if (light.type == (int)LightType::Point || light.type == (int)LightType::Spot)
    {
        // A point in the bulb (soft shadows when it has a size). The bulb itself is never hit by rays, so the
        // light is sampled only here, like the sun.
        float3 lightPosition = light.position;
        if (light.area > 0.0f)
        {
            float2 u = NextRandomFloat2(rng);
            float z = 1.0f - 2.0f * u.x;
            float r = sqrt(saturate(1.0f - z * z));
            float phi = 2.0f * PI * u.y;
            float radius = light.area * pow(NextRandomFloat(rng), 1.0f / 3.0f);
            lightPosition += float3(r * cos(phi), r * sin(phi), z) * radius;
        }

        float3 toLight = lightPosition - position;
        float distance = length(toLight);
        if (distance > 1e-4f)
        {
            float3 L = toLight / distance;
            float falloff = 1.0f;
            if (light.type == (int)LightType::Spot)
            {
                // Full inside the inner cone, none outside the outer one
                float cosAngle = dot(-L, normalize(light.normal));
                falloff = smoothstep(light.tangentX.x, light.tangentX.y, cosAngle);
            }

            if (falloff > 0.0f)
            {
                result.L = L;
                result.distance = distance;
                result.radiance = light.color * falloff / (distance * distance);
                result.pdf = light.selection;
                result.isDelta = true;
                result.valid = true;
            }
        }
    }

    return result;
}

#endif
