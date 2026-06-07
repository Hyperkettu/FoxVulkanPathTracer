// Define the payload structure matching the payload type used in your RayGen shader
struct RayPayload
{
    float3 color;
};

[shader("miss")]
void main(inout RayPayload payload)
{
    // Assign magenta to the payload color
    payload.color = float3(1.0f, 0.0f, 1.0f);
}