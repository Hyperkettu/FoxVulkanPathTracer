// The hybrid path tracer's visibility buffer, fragment stage: which draw and which triangle covers the pixel. The path
// tracer (raygen.slang) finds the rest (the exact point, the material) from these two numbers. MASKED (the variant for
// alpha tested clusters, leaves): holes where the base colour's alpha is below the material's cutoff, as the rays'
// any-hit shader decides.

#include "common.slang"

struct VertexOutput
{
    float4 position : SV_Position;
    nointerpolation uint draw : DRAW;
    nointerpolation uint triangle : TRIANGLE;
    nointerpolation uint material : MATERIAL;
    float2 uv : UV;
};

[shader("fragment")]
uint2 main(VertexOutput input) : SV_Target
{
#if defined(MASKED)
    Material material = gAllMaterials[input.material];
    float alpha = material.baseColor.a;
    if (material.baseColorTexture >= 0)
        alpha *= SampleTexture(material.baseColorTexture, TransformUV(input.uv, material)).a;
    if (alpha < material.alphaCutoff)
        discard;
#endif
    return uint2(input.draw + 1, input.triangle);
}
