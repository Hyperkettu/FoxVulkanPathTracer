// The surface reflection model of the path tracer.
//
// A layered "principled" BSDF following the glTF metallic-roughness material model:
//   - Specular:     GGX microfacet reflection (height-correlated Smith visibility, Schlick Fresnel),
//                   importance sampled with the distribution of visible normals, plus an energy
//                   compensation term so rough metals do not darken (multiple scattering).
//   - Diffuse:      Lambert, scaled by the energy the specular layer lets through.
//   - Transmission: thin-walled glass. Light that is not reflected passes straight through, tinted
//                   by the base colour (window panes; no refraction).
//
// Conventions: V points away from the surface towards the viewer, L towards the light,
// N is the shading normal with dot(N, V) > 0. All functions work in world space.

#ifndef FOX_BSDF_SLANG
#define FOX_BSDF_SLANG

#include "common.slang"

// Below this roughness a surface is treated as a perfect mirror (a delta lobe that light
// sampling cannot hit), which avoids the numerical problems of extremely narrow GGX lobes.
static const float MIRROR_ROUGHNESS = 0.04f;

static const float DIELECTRIC_F0 = 0.04f; // Reflectance at normal incidence for an index of refraction of 1.5

struct Surface
{
    float3 N;
    float3 diffuseColor;   // Albedo of the diffuse layer
    float3 F0;             // Specular reflectance at normal incidence
    float f90;             // Specular reflectance at grazing angles
    float3 transmitColor;  // Tint of light passing through (zero for opaque surfaces)
    float roughness;       // Perceptual
    float alpha;           // GGX width = roughness^2
    bool isMirror;

    // Precomputed for the current view direction
    float NdotV;
    float3 specularAlbedo;      // Fraction of light the specular layer reflects, over all directions
    float3 energyCompensation;  // Multiple scattering boost of the specular layer

    // Probability of sampling each lobe
    float pSpecular;
    float pDiffuse;
    float pTransmission;
};

// f90 is the reflectance at grazing angles: 1 for real materials, less when specular is disabled
float3 FresnelSchlick(float cosTheta, float3 F0, float f90)
{
    return F0 + (f90 - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// Analytic fit of the pre-integrated GGX environment BRDF (Karis, "Physically Based Shading on Mobile").
// Returns (scale, bias): the directional albedo of the specular layer is F0 * scale + bias.
float2 EnvironmentBRDF(float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

Surface MakeSurface(HitPayload hit, float3 N, float3 V)
{
    Surface s;
    s.N = N;
    s.NdotV = max(dot(N, V), 1e-4f);

    float3 baseColor = saturate(hit.baseColor);
    float metallic = saturate(hit.metallic);
    float transmission = saturate(hit.transmission) * (1.0f - metallic);

    s.roughness = clamp(hit.roughness, 0.0f, 1.0f);
    s.isMirror = s.roughness < MIRROR_ROUGHNESS;
    s.alpha = max(s.roughness * s.roughness, MIRROR_ROUGHNESS * MIRROR_ROUGHNESS);

    s.F0 = lerp(float3(DIELECTRIC_F0 * hit.specularWeight), baseColor, metallic);
    s.diffuseColor = baseColor * (1.0f - metallic) * (1.0f - transmission);

    float2 envBRDF = EnvironmentBRDF(s.roughness, s.NdotV);
    s.f90 = saturate(50.0f * MaxComponent(s.F0));
    s.specularAlbedo = s.F0 * envBRDF.x + envBRDF.y * s.f90;

    // Single scattering loses the energy of light bouncing between microfacets. Add it back,
    // assuming the lost part is reflected with the same colour (Kulla & Conty / Filament).
    s.energyCompensation = 1.0f + s.F0 * (1.0f / max(envBRDF.x + envBRDF.y, 1e-3f) - 1.0f);

    // What is not reflected by the specular layer is either transmitted or scattered diffusely
    float3 belowSpecular = 1.0f - s.specularAlbedo;
    s.diffuseColor *= belowSpecular;
    s.transmitColor = baseColor * transmission * belowSpecular;

    float wSpecular = Luminance(s.specularAlbedo);
    float wDiffuse = Luminance(s.diffuseColor);
    float wTransmission = Luminance(s.transmitColor);
    float wSum = max(wSpecular + wDiffuse + wTransmission, 1e-6f);

    s.pSpecular = wSpecular / wSum;
    s.pDiffuse = wDiffuse / wSum;
    s.pTransmission = wTransmission / wSum;

    // Never starve a lobe that exists: keeps the estimator's variance bounded
    if (wSpecular > 0.0f)
    {
        s.pSpecular = max(s.pSpecular, 0.1f);
        float rest = max(s.pDiffuse + s.pTransmission, 1e-6f);
        float scale = (1.0f - s.pSpecular) / rest;
        s.pDiffuse *= scale;
        s.pTransmission *= scale;
    }

    return s;
}

// --- GGX ---

float GGX_D(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * d * d);
}

// Smith masking for one direction
float GGX_G1(float NdotX, float alpha)
{
    float a2 = alpha * alpha;
    return 2.0f * NdotX / (NdotX + sqrt(a2 + (1.0f - a2) * NdotX * NdotX));
}

// Height-correlated Smith visibility, includes the 1 / (4 NdotL NdotV) of the microfacet BRDF
float GGX_Visibility(float NdotL, float NdotV, float alpha)
{
    float a2 = alpha * alpha;
    float lambdaV = NdotL * sqrt(NdotV * NdotV * (1.0f - a2) + a2);
    float lambdaL = NdotV * sqrt(NdotL * NdotL * (1.0f - a2) + a2);
    return 0.5f / max(lambdaV + lambdaL, 1e-6f);
}

// Samples a microfacet normal from the distribution of normals visible from Ve (tangent space, z up).
// Dupuy & Benyoub, "Sampling Visible GGX Normals with Spherical Caps" (2023).
float3 SampleVisibleNormal(float3 Ve, float alpha, float2 u)
{
    float3 Vh = normalize(float3(alpha * Ve.x, alpha * Ve.y, Ve.z));

    float phi = 2.0f * PI * u.x;
    float z = (1.0f - u.y) * (1.0f + Vh.z) - Vh.z;
    float sinTheta = sqrt(saturate(1.0f - z * z));
    float3 cap = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);

    float3 Nh = cap + Vh;
    return normalize(float3(alpha * Nh.x, alpha * Nh.y, max(Nh.z, 1e-6f)));
}

// Density of reflecting V into L when sampling visible normals
float VisibleNormalPdf(float NdotV, float NdotH, float alpha)
{
    return GGX_G1(NdotV, alpha) * GGX_D(NdotH, alpha) / (4.0f * NdotV);
}

// --- Evaluation ---

// Value of the non-delta lobes (diffuse + rough specular) times the cosine term, for light arriving
// from L. pdf receives the density with which SampleBsdf() would have picked L.
float3 EvalBsdf(Surface s, float3 V, float3 L, out float pdf)
{
    pdf = 0.0f;

    float NdotL = dot(s.N, L);
    if (NdotL <= 0.0f)
        return float3(0.0f);

    float3 result = s.diffuseColor * (NdotL / PI);
    pdf = s.pDiffuse * (NdotL / PI);

    if (!s.isMirror)
    {
        float3 H = normalize(V + L);
        float NdotH = saturate(dot(s.N, H));
        float VdotH = saturate(dot(V, H));

        float3 F = FresnelSchlick(VdotH, s.F0, s.f90);
        float D = GGX_D(NdotH, s.alpha);
        float visibility = GGX_Visibility(NdotL, s.NdotV, s.alpha);

        result += F * (D * visibility * NdotL) * s.energyCompensation;
        pdf += s.pSpecular * VisibleNormalPdf(s.NdotV, NdotH, s.alpha);
    }

    return result;
}

// --- Sampling ---

struct BsdfSample
{
    float3 L;          // Sampled direction
    float3 weight;     // BSDF * cosine / pdf: multiply the path throughput with this
    float pdf;         // Density of L (meaningless for delta lobes)
    bool isDelta;      // Perfect mirror or transmission: cannot be hit by light sampling
    bool isTransmission;
    bool valid;
};

BsdfSample SampleBsdf(Surface s, float3 V, inout uint rng)
{
    BsdfSample result;
    result.L = float3(0.0f);
    result.weight = float3(0.0f);
    result.pdf = 0.0f;
    result.isDelta = false;
    result.isTransmission = false;
    result.valid = false;

    float lobe = NextRandomFloat(rng);
    float2 u = NextRandomFloat2(rng);

    // Thin glass: continue straight through
    if (lobe < s.pTransmission)
    {
        result.L = -V;
        result.weight = s.transmitColor / s.pTransmission;
        result.isDelta = true;
        result.isTransmission = true;
        result.valid = true;
        return result;
    }

    float3 tangent, bitangent;
    BuildBasis(s.N, tangent, bitangent);

    bool sampledSpecular = lobe < s.pTransmission + s.pSpecular;

    if (sampledSpecular && s.isMirror)
    {
        result.L = reflect(-V, s.N);
        result.weight = FresnelSchlick(s.NdotV, s.F0, s.f90) / s.pSpecular;
        result.isDelta = true;
        result.valid = dot(s.N, result.L) > 0.0f;
        return result;
    }

    if (sampledSpecular)
    {
        float3 Ve = float3(dot(V, tangent), dot(V, bitangent), dot(V, s.N));
        float3 He = SampleVisibleNormal(Ve, s.alpha, u);
        float3 H = tangent * He.x + bitangent * He.y + s.N * He.z;
        result.L = reflect(-V, H);
    }
    else
    {
        // Cosine weighted hemisphere
        float phi = 2.0f * PI * u.x;
        float cosTheta = sqrt(1.0f - u.y);
        float sinTheta = sqrt(u.y);
        result.L = normalize(tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta) + s.N * cosTheta);
    }

    // Weight with the combined density of both lobes (one-sample MIS between them)
    float3 value = EvalBsdf(s, V, result.L, result.pdf);
    if (result.pdf <= 0.0f)
        return result;

    result.weight = value / result.pdf;
    result.valid = true;
    return result;
}

#endif
