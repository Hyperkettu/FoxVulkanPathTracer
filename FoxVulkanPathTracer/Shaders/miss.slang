// Define the payload structure matching the payload type used in your RayGen shader
struct RayPayload
{
    float3 hitColor;
    int depth;
    uint rngState;
};

[shader("miss")]
void main(inout RayPayload payload)
{
    // Assign magenta to the payload color
    payload.hitColor = float3(0.0f, 0.0f, 0.5f);
}