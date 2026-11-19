// Ray generation: the path tracer.
//
// For every pixel it
//   1. finds the surface the denoiser should see (the G-buffer), looking through mirrors and glass
//   2. traces `samplesPerPixel` light paths. Each bounce combines light sampling (environment map
//      and one analytic light) with BSDF sampling through multiple importance sampling.
//
// Paths are iterative: the closest hit shader only describes the surface, so rays never recurse.

#include "common.slang"
#include "bsdf.slang"
#include "lighting.slang"

[[vk::binding(0, 0)]] RaytracingAccelerationStructure tlas;
[[vk::binding(2, 0)]] ConstantBuffer<Camera> camera;

#include "volumes.slang"

// G-buffer of the current frame, consumed by the denoiser (see GBuffer.h for the exact contents)
[[vk::binding(1, 0)]]  [[vk::image_format("rgba16f")]] RWTexture2D<float4> gRadiance;
[[vk::binding(8, 0)]]  [[vk::image_format("rgba16f")]] RWTexture2D<float4> gAlbedo;
[[vk::binding(9, 0)]]  [[vk::image_format("rgba32f")]] RWTexture2D<float4> gNormalDepth;
[[vk::binding(10, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gMotion;
[[vk::binding(11, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gSpecular;

// Per acceleration structure instance: world space now -> world space in the previous frame (3x4, row major).
// Moved objects get motion vectors from it. Mirrors RayTracingScene::InstanceMotion.
struct InstanceMotion
{
    float4 rows[3];
    int4 lights; // x: what to add to a hit's light index (the lights of the instance's object, see closest_hit.slang)
};
[[vk::binding(13, 0)]] StructuredBuffer<InstanceMotion> gInstanceMotion;

// Object picking: x = instance index + 1 of the object seen at camera.pickPixel (0 = sky), y = distance
[[vk::binding(14, 0)]] RWStructuredBuffer<uint2> gPickResult;

// Bounces after the first hit: at most MAX_BOUNCES, camera.maxBounces of them (the render preset). Longer paths are
// also cut probabilistically (Russian roulette). Deeper bounces add little light outdoors but cost as much as the
// first ones.
static const int MAX_BOUNCES = 4;
static const int ROULETTE_START_BOUNCE = 1;

// Mirror reflections and refractions do not count as bounces, up to this many: light reaching a
// surface through a glass ball has crossed two of them, and inside glass it may reflect a few times
static const int MAX_DELTA_EVENTS = 6;

// Scattering in fog, clouds and smoke, on top of the surface bounces: clouds need several to look bright
static const int MAX_VOLUME_BOUNCES = 4;

// See EnvironmentSelectionProbability()
static const float ENVIRONMENT_LIGHT_PROBABILITY = 0.5f;

// Rougher surfaces do not trace a ray for the specular hit distance (see FindPrimarySurface)
static const float SPECULAR_DISTANCE_MAX_ROUGHNESS = 0.5f;

// Upper bound for the brightness one indirect light path may contribute. Rare, extremely bright
// paths ("fireflies") would otherwise take very long to average out. Slightly darkens caustics.
static const float INDIRECT_RADIANCE_CLAMP = 12.0f;

// How many mirrors / glass surfaces in a row the G-buffer looks through (a glass ball is two, and
// the glossy ground under a ball reflects the ball again)
static const int MAX_LOOK_THROUGH = 6;

// Glossy surfaces up to this roughness count as mirrors for the G-buffer, as long as the
// reflection dominates their look: the denoiser then keeps the edges of what is reflected.
static const float LOOK_THROUGH_ROUGHNESS = 0.2f;

static const float RAY_OFFSET = 1e-3f;
static const float RAY_MAX_DISTANCE = 1e4f;

// ------------------------------------------------------------------------------------------------
// Ray helpers
// ------------------------------------------------------------------------------------------------

// reorder: the rays of neighbouring threads go in unrelated directions (bounces after a diffuse or
// rough hit). With shader execution reordering (FOX_SER, the raygen_ser variant, chosen when the
// GPU supports it) the threads are first regrouped by what they hit, so the closest hit shader and
// the shading after it run on coherent groups: same material, same textures, same branches.
HitPayload TraceSurface(float3 origin, float3 direction, bool reorder = false)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = RAY_MAX_DISTANCE;

    HitPayload payload;
    payload.hitT = -1.0f;

#if FOX_SER
    if (reorder) {
        HitObject hitObject = HitObject::TraceRay(tlas, RAY_FLAG_NONE, INSTANCE_MASK_ALL, 0, 0, 0, ray, payload);
        ReorderThread(hitObject);
        HitObject::Invoke(tlas, hitObject, payload);
        return payload;
    }
#endif

    // Opaque geometry is flagged as such in the acceleration structures; alpha tested geometry (leaves)
    // runs the any-hit shader where its opacity micromap cannot decide
    TraceRay(tlas, RAY_FLAG_NONE, INSTANCE_MASK_ALL, 0, 0, 0, ray, payload);
    return payload;
}

// Fraction of a light's radiance that reaches origin from `direction` (0 = in shadow).
//   - Opaque surfaces block it.
//   - Thin glass (windows) lets it through unchanged.
//   - Solid glass refracts: the light that gets through arrives from other directions, which the
//     paths find themselves (a ray refracted through a glass ball reaches the sky). So shadow rays
//     towards the sky are blocked by it. Analytic lights (the sun) cannot be found that way, since a
//     scattered direction never hits them exactly: their shadow rays pass straight through solid glass
//     (throughSolidGlass), tinted and absorbed by it. The light is not focused into caustics.
float3 ShadowTransmittance(float3 origin, float3 direction, float distance, bool throughSolidGlass)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = distance;

    // Inline ray query: no shader is invoked and the path tracer's state stays in registers,
    // which makes this much cheaper than a TraceRay call for a yes/no answer.
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, throughSolidGlass ? INSTANCE_MASK_OPAQUE : INSTANCE_MASK_OPAQUE | INSTANCE_MASK_SOLID_GLASS, ray);

    // Alpha tested surfaces (leaves) come back as candidates where their opacity micromap is undecided:
    // test the texture here, as the any-hit shader does for traced rays
    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE
            && !IsAlphaHole(query.CandidateInstanceID() + query.CandidateGeometryIndex(), query.CandidatePrimitiveIndex(), query.CandidateTriangleBarycentrics()))
        {
            query.CommitNonOpaqueTriangleHit();
        }
    }

    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        return float3(0.0f);
    if (!throughSolidGlass)
        return float3(1.0f);

    // Every crossing of a solid glass surface: as non-opaque candidates that are never committed,
    // the query reports all of them
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> glass;
    glass.TraceRayInline(tlas, RAY_FLAG_NONE, INSTANCE_MASK_SOLID_GLASS, ray);

    float3 transmittance = float3(1.0f);
    float3 absorption = float3(0.0f);
    float nearest = distance;
    float farthest = 0.0f;
    uint crossings = 0;

    while (glass.Proceed())
    {
        Submesh submesh = gAllSubmeshes[glass.CandidateInstanceID() + glass.CandidateGeometryIndex()];
        Material material = gAllMaterials[submesh.materialIndex];

        // Tinted, minus what the surface reflects (at normal incidence)
        float iorF0 = (material.ior - 1.0f) / (material.ior + 1.0f);
        transmittance *= material.baseColor.rgb * (saturate(material.transmission) * (1.0f - iorF0 * iorF0));
        absorption = max(absorption, float3(material.absorptionR, material.absorptionG, material.absorptionB));

        float t = glass.CandidateTriangleRayT();
        nearest = min(nearest, t);
        farthest = max(farthest, t);
        crossings++;

        if (MaxComponent(transmittance) < 1e-3f)
        {
            glass.Abort();
            return float3(0.0f);
        }
    }

    // Distance travelled inside glass: between the first and the last crossing, or from the
    // origin when it starts inside (an odd number of crossings)
    float inside = crossings == 0 ? 0.0f : (crossings % 2 == 0 ? farthest - nearest : farthest);
    return transmittance * exp(-absorption * inside);
}

// Offsets a ray origin off the surface, to the side the ray leaves towards
float3 OffsetRayOrigin(float3 position, float3 geometricNormal, float3 direction)
{
    // Scale with the distance from the origin: float precision gets coarser far away
    float offset = RAY_OFFSET * max(1.0f, MaxComponent(abs(position)) * 0.01f);
    return position + geometricNormal * (dot(geometricNormal, direction) >= 0.0f ? offset : -offset);
}

// Makes both normals face the viewer (all surfaces are double sided)
void OrientNormals(HitPayload hit, float3 V, out float3 geometricNormal, out float3 N)
{
    geometricNormal = dot(hit.geometricNormal, V) >= 0.0f ? hit.geometricNormal : -hit.geometricNormal;
    N = dot(hit.shadingNormal, geometricNormal) >= 0.0f ? hit.shadingNormal : -hit.shadingNormal;

    // A bumped normal can end up pointing away from the viewer at grazing angles
    if (dot(N, V) <= 0.0f)
    {
        N = normalize(N - V * (dot(N, V) - 1e-3f));
    }
}

float3 PrimaryRayDirection(float2 pixelPosition)
{
    float2 d = (pixelPosition / camera.resolution) * 2.0f - 1.0f;
    d.y = -d.y;
    d *= camera.tanHalfFov;
    d.x *= camera.resolution.x / camera.resolution.y;

    return normalize(camera.camForward + d.x * camera.camRight + d.y * camera.camUp);
}

// Where a point (given relative to the previous camera position) was on screen last frame, in pixels.
// depth receives its distance along the previous view direction.
float2 ProjectToPreviousFrame(float3 fromPrevCamera, out float depth)
{
    depth = dot(fromPrevCamera, camera.prevCamForward);

    float2 d = float2(dot(fromPrevCamera, camera.prevCamRight), dot(fromPrevCamera, camera.prevCamUp)) / max(depth, 1e-6f);
    d /= camera.tanHalfFov;
    d.x /= camera.resolution.x / camera.resolution.y;
    d.y = -d.y;

    return (d * 0.5f + 0.5f) * camera.resolution;
}

// ------------------------------------------------------------------------------------------------
// Path tracing
// ------------------------------------------------------------------------------------------------

// Chance that light sampling picks the environment rather than one of the analytic lights.
// One shadow ray per bounce instead of one per light type: shadow rays towards the sky point in
// all directions, which makes them expensive, and the sky is also found by the BSDF samples.
float EnvironmentSelectionProbability()
{
    return camera.lightCount > 0 ? ENVIRONMENT_LIGHT_PROBABILITY : 1.0f;
}

// Light arriving at a surface point directly from the light sources, reflected towards V.
// Takes one sample: the environment or one of the analytic lights, with a single shadow ray.
// occlusion: the surface's ambient occlusion, applied to light from the sky (not to the sun and lamps)
float3 DirectLighting(Surface surface, float3 position, float3 geometricNormal, float3 V, float occlusion, inout uint rng)
{
    const float environmentProbability = EnvironmentSelectionProbability();

    float3 L;
    float distance;
    float3 radiance;
    float lightPdf;   // Including the choice between environment and analytic lights
    bool isDelta = false;
    bool isEnvironment = false;

    if (NextRandomFloat(rng) < environmentProbability)
    {
        float environmentPdf;
        L = SampleEnvironment(rng, environmentPdf);
        if (environmentPdf <= 0.0f)
            return float3(0.0f);

        distance = RAY_MAX_DISTANCE;
        radiance = EnvironmentRadiance(L) * occlusion;
        lightPdf = environmentPdf * environmentProbability;
        isEnvironment = true;
    }
    else
    {
        LightSample light = SampleLight(position, camera.lightCount, rng);
        if (!light.valid)
            return float3(0.0f);

        L = light.L;
        // Stop just short of the light's own surface
        distance = light.distance * (1.0f - 1e-3f) - RAY_OFFSET;
        radiance = light.radiance;
        lightPdf = light.pdf * (1.0f - environmentProbability);
        isDelta = light.isDelta;
    }

    if (dot(geometricNormal, L) <= 0.0f)
        return float3(0.0f);

    float bsdfPdf;
    float3 bsdf = EvalBsdf(surface, V, L, bsdfPdf);

    // The shadow ray is the expensive part: skip it when the light could not contribute anyway
    if (MaxComponent(bsdf * radiance) <= 0.0f)
        return float3(0.0f);

    float3 shadowOrigin = OffsetRayOrigin(position, geometricNormal, L);
    float3 transmittance = ShadowTransmittance(shadowOrigin, L, distance, !isEnvironment);
    if (MaxComponent(transmittance) <= 0.0f)
        return float3(0.0f);
    if (VolumesOn())
        // Cloud shadows from the sun and lamps; light from the sky is not marched through the clouds (too costly
        // for what it adds: the sky dims a little under them)
        transmittance *= MediaTransmittance(shadowOrigin, L, distance, camera.cloudShadows != 0 && !isEnvironment, rng);

    float weight = isDelta ? 1.0f : PowerHeuristic(lightPdf, bsdfPdf);
    return bsdf * radiance * transmittance * (weight / lightPdf);
}

// Light arriving at a point in fog, clouds or smoke directly from the light sources, scattered on along the
// direction travelled (the phase function in place of a BSDF). One light sample, as DirectLighting.
float3 DirectLightingInMedium(float3 position, float3 travel, float g, bool inCloud, inout uint rng)
{
    const float environmentProbability = EnvironmentSelectionProbability();

    float3 L;
    float distance;
    float3 radiance;
    float lightPdf;
    bool isDelta = false;
    bool isEnvironment = false;

    if (NextRandomFloat(rng) < environmentProbability)
    {
        float environmentPdf;
        L = SampleEnvironment(rng, environmentPdf);
        if (environmentPdf <= 0.0f)
            return float3(0.0f);
        distance = RAY_MAX_DISTANCE;
        radiance = EnvironmentRadiance(L);
        lightPdf = environmentPdf * environmentProbability;
        isEnvironment = true;
    }
    else
    {
        LightSample light = SampleLight(position, camera.lightCount, rng);
        if (!light.valid)
            return float3(0.0f);
        L = light.L;
        distance = light.distance * (1.0f - 1e-3f) - RAY_OFFSET;
        radiance = light.radiance;
        lightPdf = light.pdf * (1.0f - environmentProbability);
        isDelta = light.isDelta;
    }

    float phase = PhaseHG(dot(travel, L), g);
    if (MaxComponent(radiance) * phase <= 0.0f)
        return float3(0.0f);

    float3 transmittance = ShadowTransmittance(position, L, distance, !isEnvironment);
    if (MaxComponent(transmittance) <= 0.0f)
        return float3(0.0f);
    float2 depth = MediaOpticalDepth(position, L, distance, true, rng);
    // In a cloud: the light its many scattering events would bring (the path itself stops after a few)
    float scattered = inCloud ? CloudMultipleScattering(depth.x, dot(travel, L), g) : exp(-depth.x) * phase;
    transmittance *= exp(-depth.y);

    float weight = isDelta ? 1.0f : PowerHeuristic(lightPdf, phase);
    return radiance * transmittance * (scattered * weight / lightPdf);
}

float3 ClampIndirect(float3 radiance)
{
    float brightest = MaxComponent(radiance);
    return brightest > INDIRECT_RADIANCE_CLAMP ? radiance * (INDIRECT_RADIANCE_CLAMP / brightest) : radiance;
}

// Radiance arriving at origin from direction. firstHit is the result of tracing that ray when the
// caller already has it (hasFirstHit), which saves tracing the primary ray twice.
float3 TracePath(float3 origin, float3 direction, inout uint rng, HitPayload firstHit, bool hasFirstHit)
{
    float3 radiance = float3(0.0f);
    float3 throughput = float3(1.0f);

    // How the current direction was chosen, to weight lights that the path runs into
    float bsdfPdf = 0.0f;
    bool sampledDelta = true; // Camera rays and mirror bounces: light sampling could not have found this
    bool isIndirect = false;  // True once the path has scattered diffusely or glossily

    float3 mediumAbsorption = float3(0.0f); // Inside a solid: its absorption per meter

    // bounce counts diffuse and glossy scattering; mirror reflections and refractions are extra, and so is
    // scattering in the media
    int bounce = 0;
    int volumeBounce = 0;
    const bool media = VolumesOn();
    for (int segment = 0; segment <= MAX_BOUNCES + MAX_DELTA_EVENTS + MAX_VOLUME_BOUNCES; segment++)
    {
        HitPayload hit = (segment == 0 && hasFirstHit) ? firstHit : TraceSurface(origin, direction, segment > 0);

        // --- Fog, clouds, smoke: scattered on the way to the surface (or out of the scene)? ---
        if (media)
        {
            MediumHit medium = SampleMedia(origin, direction, hit.hitT < 0.0f ? VOLUME_FAR : hit.hitT, bounce + volumeBounce > 1, rng);
            if (medium.valid)
            {
                throughput *= exp(-mediumAbsorption * medium.t);
                float3 position = origin + direction * medium.t;

                // Glowing smoke gives off its light where the path meets it
                float3 glow = throughput * medium.emission;
                radiance += isIndirect ? ClampIndirect(glow) : glow;

                if (volumeBounce == MAX_VOLUME_BOUNCES)
                    break;
                volumeBounce++;

                throughput *= medium.albedo;
                float3 direct = throughput * DirectLightingInMedium(position, direction, medium.g, medium.kind == MEDIUM_CLOUD, rng);
                radiance += isIndirect ? ClampIndirect(direct) : direct;

                // Fog scatters on once; after that only its direct light is gathered (more adds little light to
                // thin fog, and nearly every ray meets fog)
                if (medium.kind == MEDIUM_FOG && isIndirect)
                    break;

                float phasePdf;
                float3 next = SamplePhaseHG(direction, medium.g, NextRandomFloat2(rng), phasePdf);
                // Sampled in proportion to the phase function: the weight is 1
                bsdfPdf = phasePdf;
                sampledDelta = false;
                isIndirect = true;
                origin = position;
                direction = next;

                float survival = clamp(MaxComponent(throughput), 0.05f, 0.95f);
                if (NextRandomFloat(rng) >= survival)
                    break;
                throughput /= survival;
                continue;
            }
        }

        // --- Left the scene: environment ---
        if (hit.hitT < 0.0f)
        {
            float weight = sampledDelta ? 1.0f : PowerHeuristic(bsdfPdf, EnvironmentSelectionProbability() * EnvironmentPdf(direction));
            float3 contribution = throughput * EnvironmentRadiance(direction) * weight;
            radiance += isIndirect ? ClampIndirect(contribution) : contribution;
            break;
        }

        // Absorbed inside glass on the way here (Beer-Lambert)
        throughput *= exp(-mediumAbsorption * hit.hitT);

        float3 position = origin + direction * hit.hitT;
        float3 V = -direction;

        float3 geometricNormal, N;
        OrientNormals(hit, V, geometricNormal, N);

        // --- Emission ---
        if (MaxComponent(hit.emission) > 0.0f)
        {
            float weight = 1.0f;
            // Light sampling could have found it too (an emissive quad or triangle): weigh the two
            int lightIndex = hit.lightIndex >= 0 ? hit.lightIndex + gInstanceMotion[hit.instanceIndex].lights.x : -1;
            if (!sampledDelta && lightIndex >= 0 && lightIndex < int(camera.lightCount))
            {
                float lightPdf = (1.0f - EnvironmentSelectionProbability())
                    * AreaLightPdf(lightIndex, hit.hitT, abs(dot(hit.geometricNormal, V)));
                weight = PowerHeuristic(bsdfPdf, lightPdf);
            }

            float3 contribution = throughput * hit.emission * weight;
            radiance += isIndirect ? ClampIndirect(contribution) : contribution;
        }

        if (bounce >= clamp(int(camera.maxBounces), 1, MAX_BOUNCES) || segment == MAX_BOUNCES + MAX_DELTA_EVENTS)
            break;

        Surface surface = MakeSurface(hit, N, V);

        // --- Direct light ---
        float3 direct = throughput * DirectLighting(surface, position, geometricNormal, V, hit.occlusion, rng);
        radiance += isIndirect ? ClampIndirect(direct) : direct;

        // --- Continue the path ---
        BsdfSample scatter = SampleBsdf(surface, V, rng);
        if (!scatter.valid)
            break;

        // Reflections must stay above the real surface and refractions below it, or light leaks through
        if (scatter.isRefraction ? dot(geometricNormal, scatter.L) >= 0.0f
            : !scatter.isTransmission && dot(geometricNormal, scatter.L) <= 0.0f)
            break;

        throughput *= scatter.weight;

        // Light gathered from the surroundings (the next bounces) is what ambient occlusion darkens.
        // Mirror-like reflections are left alone: they show a specific direction, not the surroundings.
        if (!scatter.isDelta)
            throughput *= hit.occlusion;

        if (scatter.isRefraction)
        {
            // Into the solid, or back out of it
            mediumAbsorption = surface.entering ? hit.absorption : float3(0.0f);
            sampledDelta = true;
            isIndirect = isIndirect || !surface.isMirror;
        }
        else if (!scatter.isTransmission)
        {
            // (Passing straight through a window does not change how the direction was sampled)
            bsdfPdf = scatter.pdf;
            sampledDelta = scatter.isDelta;
            isIndirect = isIndirect || !scatter.isDelta;
        }

        if (!scatter.isDelta)
            bounce++;

        origin = OffsetRayOrigin(position, geometricNormal, scatter.L);
        direction = scatter.L;

        // Russian roulette: end dim paths early, boost the survivors to stay unbiased
        if (segment >= ROULETTE_START_BOUNCE)
        {
            float survival = clamp(MaxComponent(throughput), 0.05f, 0.95f);
            if (NextRandomFloat(rng) >= survival)
                break;
            throughput /= survival;
        }
    }

    return radiance;
}

// ------------------------------------------------------------------------------------------------
// G-buffer
// ------------------------------------------------------------------------------------------------

struct PrimarySurface
{
    bool isFirstHit;       // The surface the camera ray hit, not one seen through glass or in a mirror
    float3 normal;
    float distance;        // Path length from the camera, negative for the sky
    float3 diffuseAlbedo;
    float3 specularAlbedo;
    float roughness;
    float specularHitDistance; // Distance from the surface to what its mirror reflection shows
};

// Finds the surface the denoiser should treat as visible along a primary ray. Normally that is the
// first hit. Windows are always looked through. What happens at mirrors depends on the denoiser:
//
//  - Filters that only know one surface per pixel (SVGF) get the surface seen *in* the mirror, as
//    if the mirror was a window. Otherwise they would blur everything visible in it, since the
//    mirror itself is perfectly smooth.
//  - Denoisers that track reflections themselves (DLSS Ray Reconstruction) get the mirror, plus
//    the distance to the reflected surface.
//
// firstHit is the result of tracing the primary ray, which the caller reuses for the path.
PrimarySurface FindPrimarySurface(float3 origin, float3 direction, HitPayload firstHit)
{
    PrimarySurface result;
    result.normal = float3(0.0f);
    result.distance = -1.0f;
    result.diffuseAlbedo = float3(1.0f);
    result.specularAlbedo = float3(0.0f);
    result.roughness = 1.0f;
    result.specularHitDistance = 65504.0f; // "Nothing reflected", the largest 16-bit float
    result.isFirstHit = false;

    const bool followReflections = camera.gbufferFollowsReflections != 0;

    float3 tint = float3(1.0f); // Colour picked up from the mirrors and panes on the way
    float pathLength = 0.0f;
    float3 mediumAbsorption = float3(0.0f);

    // The last reflector passed, used when its reflection turns out to show only sky
    PrimarySurface reflector = result;

    for (int i = 0; i <= MAX_LOOK_THROUGH; i++)
    {
        HitPayload hit = i == 0 ? firstHit : TraceSurface(origin, direction);
        // Sky. If it is seen in a reflection, describe the reflecting surface instead: sky
        // pixels are not denoised, but a glossy reflection of the sky is noisy and must be.
        if (hit.hitT < 0.0f)
            return reflector;

        float3 position = origin + direction * hit.hitT;
        float3 V = -direction;
        pathLength += hit.hitT;
        tint *= exp(-mediumAbsorption * hit.hitT);

        float3 geometricNormal, N;
        OrientNormals(hit, V, geometricNormal, N);
        Surface surface = MakeSurface(hit, N, V);

        bool isGlass = surface.pTransmission > 0.5f;

        // Curved glass solids (a ball) are not looked through for denoisers that follow reflections
        // (SVGF): the view through them shifts a lot with the sub-pixel jitter, which would make the
        // filter reject its history every frame. The glass surface itself is stable to filter on.
        bool opaqueToDenoiser = isGlass && surface.isSolid && followReflections;
        isGlass = isGlass && !opaqueToDenoiser;
        bool isMirror = followReflections && surface.roughness < LOOK_THROUGH_ROUGHNESS
            && Luminance(surface.specularAlbedo) > Luminance(surface.diffuseColor);

        if ((isGlass || isMirror) && i < MAX_LOOK_THROUGH)
        {
            if (isMirror)
            {
                reflector.normal = N;
                reflector.distance = pathLength;
                reflector.diffuseAlbedo = surface.diffuseColor * tint;
                reflector.specularAlbedo = surface.specularAlbedo * tint;
                reflector.roughness = surface.roughness;
            }

            // Through glass: straight on through a pane, refracted into and out of a solid (or reflected
            // inside it, past the critical angle). The view through a glass ball is what gets denoised.
            float3 next = isGlass ? direction : reflect(direction, N);
            if (isGlass && surface.isSolid)
            {
                float3 refracted = refract(direction, N, surface.eta);
                bool totalInternalReflection = dot(refracted, refracted) < 1e-6f;
                next = totalInternalReflection ? reflect(direction, N) : normalize(refracted);
                if (!totalInternalReflection)
                    mediumAbsorption = surface.entering ? hit.absorption : float3(0.0f);
            }
            tint *= isGlass ? surface.transmitColor : FresnelSchlick(surface.NdotV, surface.F0, surface.f90);

            origin = OffsetRayOrigin(position, geometricNormal, next);
            direction = next;
            continue;
        }

        // Out of look-through steps on glass or a mirror: describe the last mirror passed if there was
        // one. Otherwise this surface, with its transmission or reflection as the albedo: denoisers
        // divide by the albedo, and clear glass has no diffuse colour at all.
        bool exhausted = isGlass || isMirror;
        if (exhausted && reflector.distance >= 0.0f)
            return reflector;

        result.isFirstHit = i == 0;
        result.normal = N;
        result.distance = pathLength;
        result.diffuseAlbedo = (opaqueToDenoiser ? surface.transmitColor
            : exhausted ? (isGlass ? surface.transmitColor : surface.specularAlbedo)
            : surface.diffuseColor) * tint;
        result.specularAlbedo = surface.specularAlbedo * tint;
        result.roughness = surface.roughness;

        // The distance to what the reflection shows lets DLSS move reflections correctly. It only
        // matters where reflections are sharp enough to see; rough surfaces skip the extra ray.
        if (!followReflections && surface.roughness < SPECULAR_DISTANCE_MAX_ROUGHNESS)
        {
            float3 reflected = reflect(direction, N);
            if (dot(reflected, geometricNormal) > 0.0f)
            {
                HitPayload reflection = TraceSurface(OffsetRayOrigin(position, geometricNormal, reflected), reflected);
                if (reflection.hitT >= 0.0f)
                    result.specularHitDistance = reflection.hitT;
            }
        }

        return result;
    }

    return result;
}

[shader("raygeneration")]
void main()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint rng = InitializePixelSeed(pixel, camera.frameIndex);

    // --- G-buffer ---
    float2 primaryPosition = float2(pixel) + 0.5f + camera.jitter;
    float3 primaryDirection = PrimaryRayDirection(primaryPosition);

    // Traced once, used for both the G-buffer and the paths through the same sub-pixel position
    HitPayload primaryHit = TraceSurface(camera.camPos, primaryDirection);

    PrimarySurface primary = FindPrimarySurface(camera.camPos, primaryDirection, primaryHit);

    // Clouds, smoke or thick fog where there is only sky: sky is not denoised, but light scattered in them is
    // noisy. Described to the denoiser as a surface facing the camera where they begin, its albedo their opacity
    // along the ray: the denoiser smooths the light in them and multiplies the opacity back in, so their wisps
    // and edges stay sharp.
    if (primary.distance < 0.0f && VolumesOn())
    {
        float cloudStart = -1.0f;
        float opticalDepth = 0.0f;
        float t0, t1, depth;
        if (CloudInterval(camera.camPos, primaryDirection, VOLUME_FAR, t0, t1))
        {
            float t = March(-1, camera.camPos, primaryDirection, t0, t1, 16, 1e30f, 0.5f, depth);
            opticalDepth += depth;
            if (depth > 0.03f)
            {
                cloudStart = March(-1, camera.camPos, primaryDirection, t0, t1, 16, 0.03f, 0.5f, depth);
            }
        }
        for (uint v = 0; v < camera.volumeCount; v++)
        {
            if (VolumeInterval(camera.volumes[v], camera.camPos, primaryDirection, VOLUME_FAR, t0, t1))
            {
                March(int(v), camera.camPos, primaryDirection, t0, t1, 8, 1e30f, 0.5f, depth);
                opticalDepth += depth;
                float t = depth > 0.03f ? March(int(v), camera.camPos, primaryDirection, t0, t1, 8, 0.03f, 0.5f, depth) : -1.0f;
                if (t >= 0.0f && (cloudStart < 0.0f || t < cloudStart))
                    cloudStart = t;
            }
        }
        float fogStart = -1.0f;
        if (FogOn())
        {
            fogStart = FogCollision(camera.camPos, primaryDirection, 0.05f);
            opticalDepth += FogOpticalDepth(camera.camPos, primaryDirection, VOLUME_FAR);
        }
        float start = cloudStart >= 0.0f && (fogStart < 0.0f || cloudStart < fogStart) ? cloudStart : fogStart;
        if (start >= 0.0f && start < VOLUME_FAR)
        {
            primary.distance = start;
            primary.normal = -primaryDirection;
            primary.diffuseAlbedo = float3(max(1.0f - exp(-opticalDepth), 0.05f));
            primary.specularAlbedo = float3(0.0f);
            primary.roughness = 1.0f;
            primary.isFirstHit = false;
        }
    }
    bool isSky = primary.distance < 0.0f;

    // Motion vector: the scene is static, so only the camera moves. The sky is treated as infinitely
    // far away. For surfaces seen in a mirror, the distance is the unfolded path length, which places
    // the point behind the mirror - exactly where its reflection appears to be for a flat mirror.
    float3 primaryPoint = camera.camPos + primaryDirection * primary.distance;

    // Moving objects: where the point was last frame. Only for surfaces hit directly; what is seen
    // through glass or in a mirror moves with the camera only.
    if (!isSky && primary.isFirstHit)
    {
        // Deforming surfaces first (where the point was on the mesh), then the object's own movement
        InstanceMotion motion = gInstanceMotion[primaryHit.instanceIndex];
        float4 p = float4(primaryPoint + primaryHit.skinnedMotion, 1.0f);
        primaryPoint = float3(dot(motion.rows[0], p), dot(motion.rows[1], p), dot(motion.rows[2], p));
    }

    float prevDepth;
    float2 prevPosition = isSky
        ? ProjectToPreviousFrame(primaryDirection, prevDepth)
        : ProjectToPreviousFrame(primaryPoint - camera.prevCamPos, prevDepth);

    // Object picking for the editor gizmos
    if (all(int2(pixel) == camera.pickPixel))
    {
        bool hit = primaryHit.hitT >= 0.0f;
        gPickResult[camera.pickSlot] = uint2(hit ? primaryHit.instanceIndex + 1 : 0, asuint(hit ? primaryHit.hitT : -1.0f));
    }

    float depth = isSky ? -1.0f : primary.distance * dot(primaryDirection, camera.camForward);

    // --- Radiance ---
    float3 color = float3(0.0f);

    for (uint i = 0; i < camera.samplesPerPixel; i++)
    {
        // Spread the remaining paths over the pixel for anti-aliasing, unless the denoiser
        // wants every path at the same sub-pixel position
        if (i > 0 && camera.jitterAllSamples == 0)
        {
            float3 direction = PrimaryRayDirection(float2(pixel) + NextRandomFloat2(rng));
            color += TracePath(camera.camPos, direction, rng, primaryHit, false);
        }
        else
        {
            color += TracePath(camera.camPos, primaryDirection, rng, primaryHit, true);
        }
    }

    color /= float(max(camera.samplesPerPixel, 1u));

    // Keep the 16-bit float target finite
    if (any(isnan(color)) || any(isinf(color)))
        color = float3(0.0f);

    color = min(color, float3(60000.0f));

    gRadiance[pixel] = float4(color, 1.0f);
    gAlbedo[pixel] = float4(primary.diffuseAlbedo, 1.0f);
    gSpecular[pixel] = float4(primary.specularAlbedo, primary.roughness);
    gNormalDepth[pixel] = float4(primary.normal, depth);
    gMotion[pixel] = float4(prevPosition - primaryPosition, prevDepth, primary.specularHitDistance);
}
