using System;
using System.Numerics;
using Fox;

// Rolls a ball with a Physics component: W A S D push it (relative to where the game's camera looks), Space jumps
// while it is on something (a raycast down), B drops a box in front of it
public class PlayerBall : Script
{
    public float push = 6.0f;        // Newtons per kilogram: how hard it accelerates
    public float jump = 5.0f;        // m/s upwards
    public float maxSpeed = 8.0f;    // m/s along the ground
    public float radius = 0.4f;      // Metres: how far down the ground can be and still count as standing on it

    public override void OnUpdate(float deltaTime)
    {
        var body = Entity.Physics;
        if (!body.Exists) return;

        // Which way is forward: where the camera looks, along the ground
        Vector3 forward = Vector3.UnitZ;
        Entity camera = Scene.GameCamera;
        if (camera.IsValid)
        {
            Vector3 look = -camera.Forward; // Cameras look along -Z
            look.Y = 0.0f;
            if (look.LengthSquared() > 1e-6f) forward = Vector3.Normalize(look);
        }
        Vector3 right = Vector3.Normalize(Vector3.Cross(forward, Vector3.UnitY));

        Vector3 wish = Vector3.Zero;
        if (Input.GetKey(Key.W)) wish += forward;
        if (Input.GetKey(Key.S)) wish -= forward;
        if (Input.GetKey(Key.D)) wish += right;
        if (Input.GetKey(Key.A)) wish -= right;

        Vector3 velocity = body.Velocity;
        Vector3 flat = new Vector3(velocity.X, 0.0f, velocity.Z);
        if (wish.LengthSquared() > 0.0f && flat.Length() < maxSpeed)
        {
            body.AddImpulse(Vector3.Normalize(wish) * push * body.Mass * deltaTime);
        }

        bool grounded = Scene.Raycast(Entity.Position, -Vector3.UnitY, radius + 0.15f, out RaycastHit hit, Entity);
        if (grounded && Input.GetKeyDown(Key.Space))
        {
            body.Velocity = new Vector3(velocity.X, jump, velocity.Z);
        }

        if (Input.GetKeyDown(Key.B))
        {
            Entity box = Scene.Spawn("box", Entity.Position + forward * 2.0f + Vector3.UnitY * 2.0f);
            Debug.Log($"Dropped {box} (the scene rebuilds once for a new mesh)");
        }
    }
}
