using System.Numerics;
using Fox;

// On a trigger (a Physics component with Trigger on): glows while the player is inside it, counts visits, and plays a
// sound from where it is while the player is inside (trigger events start and stop it). With a Volume component the
// glow shows; it works without one too.
public class GoalZone : Script
{
    public string player = "Player";
    public float glowInside = 4.0f;
    public float glowOutside = 0.3f;
    public string sound = "Sounds/damn_mad_fox.mp3";
    public float soundPitch = 1.25f;

    private int visits;
    private Sound playing;

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
        playing.Stop();
        playing = Audio.PlayAt(sound, Entity.Position, 1.0f, soundPitch);
        Debug.Log($"GoalZone: sound playing: {playing.IsPlaying}");
    }

    public override void OnTriggerExit(Entity other)
    {
        if (other.Name != player) return;
        Debug.Log($"{other} left {Entity}");
        if (Entity.Volume.Exists) Entity.Volume.Glow = glowOutside;
        playing.Stop(0.5f); // Fades out
    }
}
