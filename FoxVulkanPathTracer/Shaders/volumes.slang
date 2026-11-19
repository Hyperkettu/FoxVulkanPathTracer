// Participating media for the path tracer (raygen.slang): light scattered and absorbed on its way through
//
//   - height fog: thickest at its base height and thinning exponentially above it; homogeneous across. Its
//     optical depth along a ray has a closed form, so its free paths are sampled exactly.
//   - a cloud layer: a slab of the sky between two heights, its density from noise drifting with the wind.
//   - smoke: up to MAX_VOLUMES boxes (VolumeComponent), each a soft puff or block of noisy, drifting density,
//     optionally glowing.
//
// Clouds and smoke are heterogeneous: their free paths and transmittance are found by marching a fixed number
// of jittered steps through them (regular tracking), which keeps the cost bounded. Media are grey (extinction
// is the same for every colour); their colour is the single scattering albedo. Scattering follows the
// Henyey-Greenstein phase function.
//
// The settings live in the Camera constants (common.slang, filled by MainRayTracingScene::FillVolumeConstants).
// Include after the `camera` constant buffer is declared.

#ifndef FOX_VOLUMES_SLANG
#define FOX_VOLUMES_SLANG

#include "common.slang"

static const float VOLUME_FAR = 1e5f;          // How far media reach along a ray that leaves the scene
static const float CLOUD_MAX_PATH = 30000.0f;  // Metres of the cloud layer marched at most (towards the horizon)
static const int CLOUD_STEPS = 24;            // Camera rays and the first bounce
static const int CLOUD_INDIRECT_STEPS = 12;   // Later bounces: they only need roughly the right light
static const int CLOUD_SHADOW_STEPS = 6;
static const int SMOKE_STEPS = 24;
static const int SMOKE_SHADOW_STEPS = 8;

bool FogOn() { return camera.fogColor.w > 0.5f && camera.fog.x > 0.0f; }
bool CloudsOn() { return camera.cloudShape.w > 0.5f && camera.cloudLayer.w > 0.0f; }
bool VolumesOn() { return FogOn() || CloudsOn() || camera.volumeCount > 0; }

// ------------------------------------------------------------------------------------------------
// Noise
// ------------------------------------------------------------------------------------------------

float HashLattice(int3 p)
{
    uint h = uint(p.x) * 73856093u ^ uint(p.y) * 19349663u ^ uint(p.z) * 83492791u;
    h = PCG_Hash(h);
    return float(h & 0xffffffu) * (1.0f / 16777215.0f);
}

// Smooth value noise in [0, 1]
float ValueNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = p - i;
    float3 u = f * f * (3.0f - 2.0f * f);
    int3 c = int3(i);
    float x00 = lerp(HashLattice(c + int3(0, 0, 0)), HashLattice(c + int3(1, 0, 0)), u.x);
    float x10 = lerp(HashLattice(c + int3(0, 1, 0)), HashLattice(c + int3(1, 1, 0)), u.x);
    float x01 = lerp(HashLattice(c + int3(0, 0, 1)), HashLattice(c + int3(1, 0, 1)), u.x);
    float x11 = lerp(HashLattice(c + int3(0, 1, 1)), HashLattice(c + int3(1, 1, 1)), u.x);
    return lerp(lerp(x00, x10, u.y), lerp(x01, x11, u.y), u.z);
}

// Octaves of value noise, each half as strong and about twice as fine; in [0, 1]
float Fbm(float3 p, int octaves)
{
    float sum = 0.0f, amplitude = 0.5f, total = 0.0f;
    for (int i = 0; i < octaves; i++)
    {
        sum += amplitude * ValueNoise(p);
        total += amplitude;
        // Turned a little each octave, so the lattice does not line up
        p = float3(p.x * 1.6f + p.z * 1.2f, p.y * 2.0f, -p.x * 1.2f + p.z * 1.6f) + 17.31f;
        amplitude *= 0.5f;
    }
    return sum / total;
}

// ------------------------------------------------------------------------------------------------
// Henyey-Greenstein phase function. cosTheta: between the direction travelled and the scattered one.
// ------------------------------------------------------------------------------------------------

float PhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    float denominator = max(1.0f + g2 - 2.0f * g * cosTheta, 1e-6f);
    return (1.0f - g2) / (4.0f * PI * denominator * sqrt(denominator));
}

float3 SamplePhaseHG(float3 travel, float g, float2 u, out float pdf)
{
    float cosTheta;
    if (abs(g) < 1e-3f)
    {
        cosTheta = 1.0f - 2.0f * u.x;
    }
    else
    {
        float g2 = g * g;
        float s = (1.0f - g2) / (1.0f - g + 2.0f * g * u.x);
        cosTheta = clamp((1.0f + g2 - s * s) / (2.0f * g), -1.0f, 1.0f);
    }
    float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
    float phi = 2.0f * PI * u.y;
    float3 tangent, bitangent;
    BuildBasis(travel, tangent, bitangent);
    pdf = PhaseHG(cosTheta, g);
    return normalize(tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta) + travel * cosTheta);
}

// ------------------------------------------------------------------------------------------------
// Height fog: density(y) = density * exp(-falloff * (y - base))
// ------------------------------------------------------------------------------------------------

// Its density where the ray starts, and how fast it changes along the ray (per metre)
void FogAlongRay(float3 origin, float3 direction, out float start, out float rate)
{
    float falloff = max(camera.fog.y, 0.0f);
    start = camera.fog.x * exp(min(-falloff * (origin.y - camera.fog.z), 30.0f));
    rate = falloff * direction.y;
}

float FogOpticalDepth(float3 origin, float3 direction, float distance)
{
    float start, rate;
    FogAlongRay(origin, direction, start, rate);
    float x = rate * distance;
    if (abs(x) < 1e-4f)
        return start * distance * (1.0f - 0.5f * x);
    return start * (1.0f - exp(min(-x, 80.0f))) / rate;
}

// Where along the ray the fog's optical depth reaches target; -1 if it never does
float FogCollision(float3 origin, float3 direction, float target)
{
    float start, rate;
    FogAlongRay(origin, direction, start, rate);
    if (start <= 0.0f)
        return -1.0f;
    if (abs(rate) < 1e-7f)
        return target / start;
    float x = 1.0f - target * rate / start;
    if (x <= 0.0f)
        return -1.0f; // Rising out of it before that much fog
    return -log(x) / rate;
}

// ------------------------------------------------------------------------------------------------
// Cloud layer
// ------------------------------------------------------------------------------------------------

float CloudDensity(float3 p)
{
    float bottom = camera.cloudLayer.x, top = camera.cloudLayer.y;
    float h = (p.y - bottom) / max(top - bottom, 1.0f);
    if (h <= 0.0f || h >= 1.0f)
        return 0.0f;

    // Flat bottoms, rounded tops: the layer is fullest low down and thins towards its top
    float profile = saturate(h * 6.0f) * saturate((1.0f - h) * 1.6f);

    float3 q = (p - camera.cloudWind.xyz * camera.time) / max(camera.cloudShape.x, 1.0f) * float3(1.0f, 1.8f, 1.0f);
    float threshold = (1.0f - camera.cloudLayer.z) + (1.0f - profile) * 0.35f;

    // Octaves of noise (as Fbm, 4 of them: weights 8, 4, 2, 1 over 15), stopping as soon as the rest could no
    // longer lift it over the threshold: most of the layer is clear sky, decided by the first octave
    float shape = 0.0f;
    float weight = 8.0f / 15.0f;
    float left = 1.0f;
    for (int i = 0; i < 4; i++)
    {
        shape += weight * ValueNoise(q);
        left -= weight;
        if (shape + left <= threshold)
            return 0.0f;
        q = float3(q.x * 1.6f + q.z * 1.2f, q.y * 2.0f, -q.x * 1.2f + q.z * 1.6f) + 17.31f;
        weight *= 0.5f;
    }
    return camera.cloudLayer.w * saturate((shape - threshold) * 5.0f);
}

// The part of a ray (from 0 to far) inside the cloud layer
bool CloudInterval(float3 origin, float3 direction, float far, out float t0, out float t1)
{
    t0 = 0.0f;
    t1 = 0.0f;
    if (!CloudsOn())
        return false;
    float bottom = camera.cloudLayer.x, top = camera.cloudLayer.y;
    if (abs(direction.y) < 1e-6f)
    {
        if (origin.y <= bottom || origin.y >= top)
            return false;
        t1 = min(far, CLOUD_MAX_PATH);
        return true;
    }
    float a = (bottom - origin.y) / direction.y, b = (top - origin.y) / direction.y;
    t0 = max(min(a, b), 0.0f);
    t1 = min(max(a, b), far);
    t1 = min(t1, t0 + CLOUD_MAX_PATH);
    return t0 < t1;
}

// ------------------------------------------------------------------------------------------------
// Smoke volumes
// ------------------------------------------------------------------------------------------------

float3 VolumeLocal(VolumeData v, float3 p)
{
    return float3(dot(v.rows[0], float4(p, 1.0f)), dot(v.rows[1], float4(p, 1.0f)), dot(v.rows[2], float4(p, 1.0f)));
}

float SmokeDensity(VolumeData v, float3 p)
{
    float3 local = VolumeLocal(v, p) * 2.0f; // -1..1 inside
    float r = v.shape.z > 0.5f ? length(local) : MaxComponent(abs(local));
    if (r >= 1.0f)
        return 0.0f;
    float edge = saturate((1.0f - r) / max(v.shape.y, 1e-3f));
    float strength = v.shape.x;
    if (strength <= 0.0f)
        return v.colorDensity.w * edge;
    // The noise eats into it: ragged edges, wisps and gaps (value noise stays mostly within 0.25..0.75)
    float n = Fbm((p - v.motion.xyz * camera.time) / max(v.motion.w, 0.01f), 3);
    float value = edge - strength * saturate((0.72f - n) * 2.2f);
    return v.colorDensity.w * saturate(value * 2.0f);
}

bool VolumeInterval(VolumeData v, float3 origin, float3 direction, float far, out float t0, out float t1)
{
    float3 o = VolumeLocal(v, origin);
    float3 d = float3(dot(v.rows[0].xyz, direction), dot(v.rows[1].xyz, direction), dot(v.rows[2].xyz, direction));
    d = float3(abs(d.x) < 1e-8f ? 1e-8f : d.x, abs(d.y) < 1e-8f ? 1e-8f : d.y, abs(d.z) < 1e-8f ? 1e-8f : d.z);
    float3 a = (-0.5f - o) / d, b = (0.5f - o) / d;
    float3 near = min(a, b), far3 = max(a, b);
    t0 = max(max(near.x, near.y), max(near.z, 0.0f));
    t1 = min(min(far3.x, far3.y), min(far3.z, far));
    return t0 < t1;
}

// ------------------------------------------------------------------------------------------------
// Free paths and transmittance
// ------------------------------------------------------------------------------------------------

// What the ray runs into first in the media before `far`
static const uint MEDIUM_FOG = 0;
static const uint MEDIUM_CLOUD = 1;
static const uint MEDIUM_SMOKE = 2;

struct MediumHit
{
    bool valid;
    uint kind;        // MEDIUM_*
    float t;
    float3 albedo;    // Of the scattering (the rest is absorbed)
    float3 emission;  // Radiance given off there (glowing smoke)
    float g;          // Phase function anisotropy
};

// Marches [t0, t1] through clouds (volume < 0) or a smoke volume, for the distance where the optical depth
// reaches target (-1: not reached); depth receives the optical depth marched
float March(int volume, float3 origin, float3 direction, float t0, float t1, int steps, float target, float jitter, out float depth)
{
    float dt = (t1 - t0) / float(steps);
    depth = 0.0f;
    for (int i = 0; i < steps; i++)
    {
        float t = t0 + (float(i) + jitter) * dt;
        float3 p = origin + direction * t;
        float sigma = volume < 0 ? CloudDensity(p) : SmokeDensity(camera.volumes[volume], p);
        float next = depth + sigma * dt;
        if (next >= target)
            return t0 + float(i) * dt + (target - depth) / max(sigma, 1e-9f);
        depth = next;
    }
    return -1.0f;
}

// indirect: a later bounce (fewer steps through the clouds)
MediumHit SampleMedia(float3 origin, float3 direction, float far, bool indirect, inout uint rng)
{
    MediumHit result;
    result.valid = false;
    result.kind = MEDIUM_FOG;
    result.t = far;
    result.albedo = float3(0.0f);
    result.emission = float3(0.0f);
    result.g = 0.0f;

    // Each medium on its own: the nearest of their collisions is the first one in all of them together
    if (FogOn())
    {
        float t = FogCollision(origin, direction, -log(1.0f - NextRandomFloat(rng)));
        if (t >= 0.0f && t < result.t)
        {
            result.valid = true;
            result.t = t;
            result.kind = MEDIUM_FOG;
            result.albedo = camera.fogColor.rgb;
            result.emission = float3(0.0f);
            result.g = camera.fog.w;
        }
    }

    float t0, t1, depth;
    if (CloudInterval(origin, direction, result.t, t0, t1))
    {
        float t = March(-1, origin, direction, t0, t1, indirect ? CLOUD_INDIRECT_STEPS : CLOUD_STEPS, -log(1.0f - NextRandomFloat(rng)), NextRandomFloat(rng), depth);
        if (t >= 0.0f && t < result.t)
        {
            result.valid = true;
            result.t = t;
            result.kind = MEDIUM_CLOUD;
            result.albedo = float3(camera.cloudWind.w);
            result.emission = float3(0.0f);
            result.g = camera.cloudShape.z;
        }
    }

    for (uint i = 0; i < camera.volumeCount; i++)
    {
        VolumeData v = camera.volumes[i];
        if (!VolumeInterval(v, origin, direction, result.t, t0, t1))
            continue;
        float t = March(int(i), origin, direction, t0, t1, SMOKE_STEPS, -log(1.0f - NextRandomFloat(rng)), NextRandomFloat(rng), depth);
        if (t >= 0.0f && t < result.t)
        {
            result.valid = true;
            result.t = t;
            result.kind = MEDIUM_SMOKE;
            result.albedo = v.colorDensity.rgb;
            result.emission = v.emission.rgb;
            result.g = v.emission.w;
        }
    }
    return result;
}

// Optical depth of the media between origin and distance along direction: x the cloud layer (when clouds),
// y fog and smoke
float2 MediaOpticalDepth(float3 origin, float3 direction, float distance, bool clouds, inout uint rng)
{
    float far = min(distance, VOLUME_FAR);
    float depth = 0.0f;
    if (FogOn())
        depth += FogOpticalDepth(origin, direction, far);

    float t0, t1, marched;
    float cloud = 0.0f;
    if (clouds && CloudInterval(origin, direction, far, t0, t1))
    {
        March(-1, origin, direction, t0, t1, CLOUD_SHADOW_STEPS, 1e30f, NextRandomFloat(rng), marched);
        cloud = marched;
    }
    for (uint i = 0; i < camera.volumeCount; i++)
    {
        if (depth > 20.0f)
            break;
        if (VolumeInterval(camera.volumes[i], origin, direction, far, t0, t1))
        {
            March(int(i), origin, direction, t0, t1, SMOKE_SHADOW_STEPS, 1e30f, NextRandomFloat(rng), marched);
            depth += marched;
        }
    }
    return float2(cloud, depth);
}

// Fraction of light that passes through the media between origin and distance along direction. clouds:
// include the cloud layer (shadow rays from surfaces leave it out when clouds cast no shadows)
float MediaTransmittance(float3 origin, float3 direction, float distance, bool clouds, inout uint rng)
{
    float2 depth = MediaOpticalDepth(origin, direction, distance, clouds, rng);
    return exp(-(depth.x + depth.y));
}

// Light reaching a point inside a cloud from a light, as the many scattering events in it would carry it: the
// paths stop after a few, which would leave the inside of thick clouds black. Sums octaves of ever weaker
// extinction and ever less forward scattering (Wrenninge et al., "Oz: the great and volumetric"), normalised
// so thin cloud is unchanged. Returns transmittance times phase.
float CloudMultipleScattering(float cloudDepth, float cosTheta, float g)
{
    float sum = 0.0f, total = 0.0f, a = 1.0f, b = 1.0f, c = 1.0f;
    for (int i = 0; i < 3; i++)
    {
        sum += a * exp(-cloudDepth * b) * PhaseHG(cosTheta, g * c);
        total += a;
        a *= 0.5f;
        b *= 0.3f;
        c *= 0.5f;
    }
    return sum / total;
}

#endif
