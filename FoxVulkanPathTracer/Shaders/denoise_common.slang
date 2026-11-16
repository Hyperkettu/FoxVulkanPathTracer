// Helpers shared by the denoising compute shaders.

#ifndef FOX_DENOISE_COMMON_SLANG
#define FOX_DENOISE_COMMON_SLANG

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// The G-buffer stores a depth <= 0 for pixels where the primary ray left the scene.
bool IsSky(float depth)
{
    return depth <= 0.0f;
}

// The reflectance the lighting of a pixel was (approximately) multiplied with: the diffuse albedo
// plus what the specular layer reflects. Has a floor, so dividing by it (demodulation) is always safe.
float3 DemodulationAlbedo(float3 diffuseAlbedo, float3 specularAlbedo)
{
    return max(diffuseAlbedo + specularAlbedo, float3(0.01f));
}

// How similar two surface normals are, in [0, 1].
float NormalWeight(float3 centerNormal, float3 sampleNormal, float power)
{
    return pow(saturate(dot(centerNormal, sampleNormal)), power);
}

// How likely two depths belong to the same surface. depthSlope is the expected depth
// change per pixel at the center, pixelDistance how far apart the two pixels are.
float DepthWeight(float centerDepth, float sampleDepth, float depthSlope, float pixelDistance)
{
    const float tolerance = max(depthSlope * pixelDistance, 1e-4f * centerDepth);
    return exp(-abs(centerDepth - sampleDepth) / tolerance);
}

#endif
