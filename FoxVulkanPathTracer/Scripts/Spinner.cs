using System;
using System.Numerics;
using Fox;

// Turns its entity about the vertical axis, and bobs it up and down
public class Spinner : Script
{
    public float degreesPerSecond = 90.0f;
    public float bobHeight = 0.0f;      // Metres
    public float bobPerSecond = 0.5f;   // Bobs per second

    private Vector3 start;
    private float time;

    public override void OnStart()
    {
        start = Entity.Position;
    }

    public override void OnUpdate(float deltaTime)
    {
        time += deltaTime;
        Entity.Rotate(Vector3.UnitY, degreesPerSecond * deltaTime);
        if (bobHeight != 0.0f)
        {
            Entity.Position = start + Vector3.UnitY * (bobHeight * MathF.Sin(time * bobPerSecond * 2.0f * MathF.PI));
        }
    }
}
