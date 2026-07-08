struct ShadowPayload
{
	bool isShadowed;
};

[shader("miss")]
void main(inout ShadowPayload payload)
{
	payload.isShadowed = false;
}