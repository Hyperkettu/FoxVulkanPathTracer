using Fox;

// Background music from a script: plays a track when the game starts, looping or once, and the Interact action (F)
// stops it and starts it again. (A Sound component with Background music on does the same without a script.)
public class MusicPlayer : Script
{
    public string file = "Sounds/damn_mad_fox.mp3";
    public bool loop = true;
    public float volume = 0.5f;
    public float fadeSeconds = 1.5f;

    private Sound music;

    public override void OnStart()
    {
        music = Audio.PlayMusic(file, loop, volume, fadeSeconds);
        Debug.Log($"MusicPlayer: {file} ({(loop ? "looping" : "once")}), playing: {music.IsPlaying}");
    }

    public override void OnUpdate(float deltaTime)
    {
        if (Input.GetActionDown("Interact"))
        {
            if (music.IsPlaying)
            {
                Audio.StopMusic(fadeSeconds);
                Debug.Log("MusicPlayer: stopped");
            }
            else
            {
                music = Audio.PlayMusic(file, loop, volume, fadeSeconds);
                Debug.Log($"MusicPlayer: playing again: {music.IsPlaying}");
            }
        }
    }
}
