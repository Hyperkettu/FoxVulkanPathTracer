using System;
using System.Numerics;
using Fox;

// On an entity with a Camera component: follows another entity (by name) from behind and above, looking at it.
// The mouse turns it around the target while the view is captured (outside edit mode).
public class FollowCamera : Script
{
    public string target = "Player";
    public float distance = 6.0f;
    public float height = 2.5f;
    public float smoothing = 6.0f;          // How quickly it catches up (per second)
    public float mouseDegreesPerPixel = 0.2f;

    private Entity followed;
    private float yaw;                       // Degrees around the target

    public override void OnStart()
    {
        followed = Entity.Find(target);
        if (!followed.IsValid) Debug.Warn($"FollowCamera: no entity called \"{target}\"");
    }

    public override void OnUpdate(float deltaTime)
    {
        if (!followed.IsValid) return;
        yaw += Input.MouseDelta.X * mouseDegreesPerPixel;

        float radians = yaw * MathF.PI / 180.0f;
        Vector3 behind = new Vector3(MathF.Sin(radians), 0.0f, -MathF.Cos(radians)) * distance;
        Vector3 wanted = followed.Position + behind + Vector3.UnitY * height;
        float blend = 1.0f - MathF.Exp(-smoothing * deltaTime);
        Entity.Position = Vector3.Lerp(Entity.Position, wanted, blend);
        Entity.AimAt(followed.Position + Vector3.UnitY * 0.5f);
    }
}
