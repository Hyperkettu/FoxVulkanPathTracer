using System;
using System.Numerics;
using Fox;

// Rolls a ball with a Physics component, by named input (Window > Input Actions): the MoveX / MoveY axes push it
// (relative to where the game's camera looks), Jump jumps while it is on something (a raycast down), Sprint pushes
// harder, Blast throws what is near away (an overlap), Drop drops a box in front of it. Hard knocks are logged
// (collision events).
public class PlayerBall : Script
{
    public float push = 6.0f;        // Newtons per kilogram: how hard it accelerates
    public float jump = 5.0f;        // m/s upwards
    public float maxSpeed = 8.0f;    // m/s along the ground
    public float radius = 0.4f;      // Metres: how far down the ground can be and still count as standing on it
    public float blastRadius = 5.0f;
    public float blastSpeed = 6.0f;  // m/s given to what the blast reaches

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

        Vector2 move = Input.GetVector("MoveX", "MoveY");
        Vector3 wish = right * move.X + forward * move.Y;
        float strength = Input.GetAction("Sprint") ? 1.8f : 1.0f;

        Vector3 velocity = body.Velocity;
        Vector3 flat = new Vector3(velocity.X, 0.0f, velocity.Z);
        if (wish.LengthSquared() > 0.0f && flat.Length() < maxSpeed * strength)
        {
            body.AddImpulse(wish * push * strength * body.Mass * deltaTime);
        }

        // Standing on the ground or on anything solid (not on triggers, which a raycast ignores unless asked)
        bool grounded = Physics.Raycast(Entity.Position, -Vector3.UnitY, radius + 0.15f, out RaycastHit hit, Layers.All, Entity);
        if (grounded && Input.GetActionDown("Jump"))
        {
            body.Velocity = new Vector3(velocity.X, jump, velocity.Z);
        }

        if (Input.GetActionDown("Blast"))
        {
            foreach (Entity near in Physics.OverlapSphere(Entity.Position, blastRadius, Layers.All, Entity))
            {
                var other = near.Physics;
                if (!other.Exists || other.Mass <= 0.0f) continue; // Static and kinematic bodies stay put
                Vector3 away = near.Position - Entity.Position + Vector3.UnitY;
                other.AddImpulse(Vector3.Normalize(away) * blastSpeed * other.Mass);
            }
        }

        if (Input.GetActionDown("Drop"))
        {
            Entity box = Scene.Spawn("box", Entity.Position + forward * 2.0f + Vector3.UnitY * 2.0f);
            Debug.Log($"Dropped {box}");
        }
    }

    public override void OnCollisionEnter(Collision collision)
    {
        if (collision.Speed > 4.0f)
            Debug.Log($"{Entity} hit {(collision.Other.IsValid ? collision.Other.Name : "the ground")} at {collision.Speed:F1} m/s");
    }
}
