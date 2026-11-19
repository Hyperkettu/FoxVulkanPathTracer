using System;
using System.Numerics;
using Fox;

// On an entity with a Nav Agent: walks after another entity (by name), planning a new way every so often
public class Chaser : Script
{
    public string target = "Player";
    public float replanSeconds = 0.75f;
    public float closeEnough = 1.5f;   // Metres: it stops this near

    private Entity followed;
    private float timer;

    public override void OnStart()
    {
        followed = Entity.Find(target);
        if (!Entity.Agent.Exists) Debug.Warn($"Chaser on {Entity}: it needs a Nav Agent component");
    }

    public override void OnUpdate(float deltaTime)
    {
        if (!followed.IsValid || !Entity.Agent.Exists) return;
        timer -= deltaTime;
        float gap = Vector3.Distance(Entity.Position, followed.Position);
        if (gap < closeEnough)
        {
            Entity.Agent.Stop();
            return;
        }
        if (timer <= 0.0f)
        {
            timer = replanSeconds;
            Entity.Agent.Target = followed.Position;
        }
    }
}
