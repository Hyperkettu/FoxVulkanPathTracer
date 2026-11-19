using System.Numerics;
using Fox;

public class Mover : Script
{
    public float speed = 1.0f; // Public fields show in the Inspector

    public override void OnStart()
    {
        Debug.Log($"{Entity.Name} starts");
    }

    public override void OnUpdate(float deltaTime)
    {
    }
}
