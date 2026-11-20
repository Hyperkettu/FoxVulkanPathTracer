using Fox;

// On a UI button (UIButtonComponent): a click switches an entity on for a while, then off again. With a Rain particle
// emitter as the target (made inactive in the scene) it rains for that long; the rain eases in and out by its ramp,
// and the scene dries afterwards. A click while it is on starts the time again.
public class RainOnClick : Script
{
    public string target = "Rain";
    public float seconds = 10.0f;

    private Entity rain;
    private float left;

    public override void OnStart()
    {
        rain = Scene.Find(target);
        if (!rain.IsValid) Debug.Log($"RainOnClick: no entity named {target}");
    }

    public override void OnUpdate(float deltaTime)
    {
        if (!rain.IsValid) return;
        if (UI.Clicked(Entity))
        {
            left = seconds;
            rain.Active = true;
            Debug.Log($"RainOnClick: {target} for {seconds} s");
        }
        if (left > 0.0f)
        {
            left -= deltaTime;
            if (left <= 0.0f)
            {
                rain.Active = false;
                Debug.Log($"RainOnClick: {target} stops");
            }
        }
    }
}
