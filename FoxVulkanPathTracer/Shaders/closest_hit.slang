// Define the payload structure matching the payload type used in your RayGen shader
/*struct RayPayload
{
    float3 color;
};

[shader("closesthit")]
void main(inout RayPayload payload)
{
    // Assign magenta to the payload color
    payload.color = float3(0.0f, 0.5f, 0.0f);
} v*/

// 3. Define the Ray Payload matching your Ray Generation stage
struct RayPayload {
    float3 hitColor;
};

// Closest Hit entry point
[shader("closesthit")]
void main(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attribs) {
    
    // Retrieve the custom index you passed via C++ 'instanceCustomIndex' 
    // when setting up the VkAccelerationStructureInstanceKHR
    uint customInstanceID = InstanceID();

    // Prevent buffer overflow by clamping to the max array size (0 to 5)
    uint safeIndex = clamp(customInstanceID, 0, 5);

    // FIXED: Slang/HLSL uses curly braces for array literal initialization
    float4 colors[6] = {
        float4(1.0, 0.0, 0.0, 1.0),
        float4(1.0, 1.0, 0.0, 1.0),
        float4(1.0, 0.5, 1.0, 1.0),
        float4(0.0, 1.0, 1.0, 1.0),
        float4(0.0, 1.0, 0.0, 1.0),
        float4(0.0, 0.0, 1.0, 1.0)
    };

    // Pull the color out of the local palette array
    float4 objectColor = colors[safeIndex];

    // Assign the RGB channels to your ray's payload
    payload.hitColor = objectColor.xyz;
}