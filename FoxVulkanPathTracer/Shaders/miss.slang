#include "common.slang"

// The ray left the scene. The path tracer looks up the environment itself, because it needs
// the same lookup when it samples the environment as a light.
[shader("miss")]
void main(inout HitPayload payload)
{
    payload.hitT = -1.0f;
}
