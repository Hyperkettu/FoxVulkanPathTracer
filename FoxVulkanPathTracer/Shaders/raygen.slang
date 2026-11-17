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

// G-buffer of the current frame, consumed by the denoiser (see GBuffer.h for the exact contents)
[[vk::binding(1, 0)]]  [[vk::image_format("rgba16f")]] RWTexture2D<float4> gRadiance;
[[vk::binding(8, 0)]]  [[vk::image_format("rgba16f")]] RWTexture2D<float4> gAlbedo;
[[vk::binding(9, 0)]]  [[vk::image_format("rgba32f")]] RWTexture2D<float4> gNormalDepth;
[[vk::binding(10, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gMotion;
[[vk::binding(11, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> gSpecular;

// Bounces after the first hit. Longer paths are also cut probabilistically (Russian roulette).
// Deeper bounces add little light outdoors but cost as much as the first ones.
static const int MAX_BOUNCES = 4;
static const int ROULETTE_START_BOUNCE = 1;

// See EnvironmentSelectionProbability()
static const float ENVIRONMENT_LIGHT_PROBABILITY = 0.5f;

// Rougher surfaces do not trace a ray for the specular hit distance (see FindPrimarySurface)
static const float SPECULAR_DISTANCE_MAX_ROUGHNESS = 0.5f;

// Upper bound for the brightness one indirect light path may contribute. Rare, extremely bright
// paths ("fireflies") would otherwise take very long to average out. Slightly darkens caustics.
static const float INDIRECT_RADIANCE_CLAMP = 12.0f;

// How many mirrors / glass panes in a row the G-buffer looks through
static const int MAX_LOOK_THROUGH = 3;

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
        HitObject hitObject = HitObject::TraceRay(tlas, RAY_FLAG_FORCE_OPAQUE, INSTANCE_MASK_ALL, 0, 0, 0, ray, payload);
        ReorderThread(hitObject);
        HitObject::Invoke(tlas, hitObject, payload);
        return payload;
    }
#endif

    TraceRay(tlas, RAY_FLAG_FORCE_OPAQUE, INSTANCE_MASK_ALL, 0, 0, 0, ray, payload);
    return payload;
}

// Is anything between origin and the light? Windows do not block light: shadow rays skip glass.
bool IsOccluded(float3 origin, float3 direction, float distance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = distance;

    // Inline ray query: no shader is invoked and the path tracer's state stays in registers,
    // which makes this much cheaper than a TraceRay call for a yes/no answer.
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, INSTANCE_MASK_OPAQUE, ray);
    query.Proceed();

    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
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
float3 DirectLighting(Surface surface, float3 position, float3 geometricNormal, float3 V, inout uint rng)
{
    const float environmentProbability = EnvironmentSelectionProbability();

    float3 L;
    float distance;
    float3 radiance;
    float lightPdf;   // Including the choice between environment and analytic lights
    bool isDelta = false;

    if (NextRandomFloat(rng) < environmentProbability)
    {
        float environmentPdf;
        L = SampleEnvironment(rng, environmentPdf);
        if (environmentPdf <= 0.0f)
            return float3(0.0f);

        distance = RAY_MAX_DISTANCE;
        radiance = EnvironmentRadiance(L);
        lightPdf = environmentPdf * environmentProbability;
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
    if (MaxComponent(bsdf * radiance) <= 0.0f || IsOccluded(OffsetRayOrigin(position, geometricNormal, L), L, distance))
        return float3(0.0f);

    float weight = isDelta ? 1.0f : PowerHeuristic(lightPdf, bsdfPdf);
    return bsdf * radiance * (weight / lightPdf);
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

    for (int bounce = 0; bounce <= MAX_BOUNCES; bounce++)
    {
        HitPayload hit = (bounce == 0 && hasFirstHit) ? firstHit : TraceSurface(origin, direction, bounce > 0);

        // --- Left the scene: environment ---
        if (hit.hitT < 0.0f)
        {
            float weight = sampledDelta ? 1.0f : PowerHeuristic(bsdfPdf, EnvironmentSelectionProbability() * EnvironmentPdf(direction));
            float3 contribution = throughput * EnvironmentRadiance(direction) * weight;
            radiance += isIndirect ? ClampIndirect(contribution) : contribution;
            break;
        }

        float3 position = origin + direction * hit.hitT;
        float3 V = -direction;

        float3 geometricNormal, N;
        OrientNormals(hit, V, geometricNormal, N);

        // --- Emission ---
        if (MaxComponent(hit.emission) > 0.0f)
        {
            float weight = 1.0f;
            if (!sampledDelta && hit.lightIndex >= 0)
            {
                float lightPdf = (1.0f - EnvironmentSelectionProbability())
                    * AreaLightPdf(hit.lightIndex, camera.lightCount, hit.hitT, abs(dot(hit.geometricNormal, V)));
                weight = PowerHeuristic(bsdfPdf, lightPdf);
            }

            float3 contribution = throughput * hit.emission * weight;
            radiance += isIndirect ? ClampIndirect(contribution) : contribution;
        }

        if (bounce == MAX_BOUNCES)
            break;

        Surface surface = MakeSurface(hit, N, V);

        // --- Direct light ---
        float3 direct = throughput * DirectLighting(surface, position, geometricNormal, V, rng);
        radiance += isIndirect ? ClampIndirect(direct) : direct;

        // --- Continue the path ---
        BsdfSample scatter = SampleBsdf(surface, V, rng);
        if (!scatter.valid)
            break;

        // Reflections must stay above the real surface, or light leaks through it
        if (!scatter.isTransmission && dot(geometricNormal, scatter.L) <= 0.0f)
            break;

        throughput *= scatter.weight;

        // Passing straight through a window does not change how the direction was sampled
        if (!scatter.isTransmission)
        {
            bsdfPdf = scatter.pdf;
            sampledDelta = scatter.isDelta;
            isIndirect = isIndirect || !scatter.isDelta;
        }

        origin = OffsetRayOrigin(position, geometricNormal, scatter.L);
        direction = scatter.L;

        // Russian roulette: end dim paths early, boost the survivors to stay unbiased
        if (bounce >= ROULETTE_START_BOUNCE)
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

    const bool followReflections = camera.gbufferFollowsReflections != 0;

    float3 tint = float3(1.0f); // Colour picked up from the mirrors and panes on the way
    float pathLength = 0.0f;

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

        float3 geometricNormal, N;
        OrientNormals(hit, V, geometricNormal, N);
        Surface surface = MakeSurface(hit, N, V);

        bool isGlass = surface.pTransmission > 0.5f;
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

            float3 next = isGlass ? direction : reflect(direction, N);
            tint *= isGlass ? surface.transmitColor : FresnelSchlick(surface.NdotV, surface.F0, surface.f90);

            origin = OffsetRayOrigin(position, geometricNormal, next);
            direction = next;
            continue;
        }

        result.normal = N;
        result.distance = pathLength;
        result.diffuseAlbedo = surface.diffuseColor * tint;
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
    bool isSky = primary.distance < 0.0f;

    // Motion vector: the scene is static, so only the camera moves. The sky is treated as infinitely
    // far away. For surfaces seen in a mirror, the distance is the unfolded path length, which places
    // the point behind the mirror - exactly where its reflection appears to be for a flat mirror.
    float prevDepth;
    float2 prevPosition = isSky
        ? ProjectToPreviousFrame(primaryDirection, prevDepth)
        : ProjectToPreviousFrame(camera.camPos + primaryDirection * primary.distance - camera.prevCamPos, prevDepth);

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
