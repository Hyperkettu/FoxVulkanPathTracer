// 2D overlay (editor gizmos): flat colour, alpha blended by the pipeline

struct FragmentInput
{
    [[vk::location(0)]] float4 color : COLOR;
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target
{
    return input.color;
}
