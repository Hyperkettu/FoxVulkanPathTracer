using System.Numerics;
using Fox;

// On a trigger (a Physics component with Trigger on): glows while the player is inside it, and counts visits.
// With a Volume component the glow shows; it works without one too.
public class GoalZone : Script
{
    public string player = "Player";
    public float glowInside = 4.0f;
    public float glowOutside = 0.3f;

    private int visits;

    public override void OnStart()
    {
        if (!Entity.Physics.Exists || !Entity.Physics.IsTrigger)
            Debug.Warn($"GoalZone on {Entity}: give it a Physics component with Trigger on");
        if (Entity.Volume.Exists) Entity.Volume.Glow = glowOutside;
    }

    public override void OnTriggerEnter(Entity other)
    {
        if (other.Name != player) return;
        visits++;
        Debug.Log($"{other} reached {Entity} ({visits} time{(visits == 1 ? "" : "s")})");
        if (Entity.Volume.Exists) Entity.Volume.Glow = glowInside;
    }

    public override void OnTriggerExit(Entity other)
    {
        if (other.Name != player) return;
        Debug.Log($"{other} left {Entity}");
        if (Entity.Volume.Exists) Entity.Volume.Glow = glowOutside;
    }
}
