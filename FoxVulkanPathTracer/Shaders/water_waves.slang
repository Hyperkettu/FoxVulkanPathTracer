// The wind's ripples on water: waves finer than the simulation's cells (Scene/WaterSimulation.h simulates the
// rest), added to the shading normal where a ray hits water.
//
// A sum of waves whose lengths run from capillary ripples (1.5 cm) to short gravity waves (60 cm), spread
// around the wind's direction (short ones more widely). Each travels at its own speed, from the dispersion
// relation of waves on deep water with surface tension:
//
//     omega^2 = g k + (sigma / rho) k^3
//
// Short ripples rise in the lightest wind; longer waves need more. Waves shorter than a few pixels at the hit's
// distance are left out (they would only flicker) and their slopes added to the roughness instead, so distant
// water keeps the same broad sheen (as LEAN mapping does for normal maps).

#ifndef WATER_WAVES_SLANG
#define WATER_WAVES_SLANG

static const int WATER_MATERIAL = -2; // Material.layerMaterial of water

// N: the surface's normal; position: world; wind: m/s along x and z; strength: 0..2; footprint: the size of a
// pixel at the hit (metres). roughness: increased by the waves too fine to show.
float3 WaterRipples(float3 N, float3 position, float2 wind, float strength, float time, float footprint, inout float roughness)
{
    float speed = length(wind);
    if (speed < 0.05f || strength <= 0.0f)
        return N;
    float2 direction = wind / speed;

    const int WAVES = 24;
    const float SHORTEST = 0.015f, LONGEST = 0.6f;
    float2 gradient = float2(0.0f, 0.0f);
    float hiddenSlopeVariance = 0.0f;

    for (int w = 0; w < WAVES; w++)
    {
        float t = (w + 0.5f) / WAVES;
        float wavelength = SHORTEST * pow(LONGEST / SHORTEST, t);
        float k = 2.0f * 3.14159265f / wavelength;
        float omega = sqrt(9.81f * k + 7.28e-5f * k * k * k);

        // Fixed per wave: its direction around the wind and its phase
        float hashA = frac(sin(w * 12.9898f + 1.0f) * 43758.5453f);
        float hashB = frac(sin(w * 78.233f + 2.0f) * 12345.6789f);
        float angle = (hashA - 0.5f) * lerp(2.4f, 1.0f, t);
        float2 d = float2(direction.x * cos(angle) - direction.y * sin(angle), direction.x * sin(angle) + direction.y * cos(angle));

        // Steepness (k * amplitude): short waves rise in a light breeze, long ones need a stronger wind
        float steepness = strength * 0.11f * saturate(speed / (0.8f + 16.0f * wavelength)) / sqrt(WAVES / 6.0f);

        // Shown where a wave spans several pixels; the rest goes to the roughness
        float shown = saturate(wavelength / (4.0f * footprint) - 0.5f);
        float phase = k * dot(d, position.xz) - omega * time + hashB * 6.2831853f;
        // Height a cos(phase): its slope is -k a sin(phase) along d
        gradient += -steepness * sin(phase) * d * shown;
        hiddenSlopeVariance += 0.5f * steepness * steepness * (1.0f - shown * shown);
    }

    // Slope variance as GGX roughness (alpha^2 grows by twice the variance)
    float alpha = roughness * roughness;
    roughness = sqrt(sqrt(alpha * alpha + 2.0f * hiddenSlopeVariance));

    // The surface y = h(x, z) leans by its gradient
    return normalize(N + float3(-gradient.x, 0.0f, -gradient.y));
}

// --- Foam: how much of the surface it covers ---
// The simulation gives an amount per vertex (0: none, 1 and more: thick). Foam is not an even film: it gathers in
// streaks and patches of bubbles with open water between, which thin out as it fades. A threshold over two octaves
// of noise drifting with the wind turns the amount into coverage: little foam is a few specks, lots is a sheet.
float WaterFoamHash(float2 p)
{
    p = frac(p * float2(0.1031f, 0.1030f));
    p += dot(p, p.yx + 33.33f);
    return frac((p.x + p.y) * p.x);
}

float WaterFoamNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 w = f * f * (3.0f - 2.0f * f);
    float a = WaterFoamHash(i);
    float b = WaterFoamHash(i + float2(1.0f, 0.0f));
    float c = WaterFoamHash(i + float2(0.0f, 1.0f));
    float d = WaterFoamHash(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}

float WaterFoamCoverage(float amount, float3 position, float2 wind, float time, float footprint)
{
    if (amount <= 0.001f)
    {
        return 0.0f;
    }
    float2 p = position.xz - wind * (0.03f * time);
    float n = 0.65f * WaterFoamNoise(p * 2.3f) + 0.35f * WaterFoamNoise(p * 9.1f + 17.0f);
    // From afar the patches are finer than a pixel: fade to their average instead of flickering
    float detail = saturate(1.0f - footprint * 4.0f);
    n = lerp(0.5f, n, detail);
    float a = saturate(amount);
    float threshold = 1.0f - a;
    float edge = lerp(0.5f, 0.12f, detail);
    return saturate((n - threshold + 0.5f * a) / edge + 0.5f) * saturate(amount * 3.0f);
}

#endif
