// The engine's functions that scripts call, and the scripting entry points the engine calls. Mirrors
// Scripting/ScriptApi.h field by field: the order of the fields is the contract.

using System.Runtime.InteropServices;

namespace Fox.Interop
{
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct NativeApi
    {
        public delegate* unmanaged<int, byte*, void> Log;

        public delegate* unmanaged<byte*, ulong> FindEntity;
        public delegate* unmanaged<ulong, int> IsValid;
        public delegate* unmanaged<ulong, byte*, int, int> GetName;
        public delegate* unmanaged<ulong, byte*, void> SetName;
        public delegate* unmanaged<ulong, int> GetActive;
        public delegate* unmanaged<ulong, int, void> SetActive;
        public delegate* unmanaged<ulong, ulong> GetParent;
        public delegate* unmanaged<ulong, ulong, void> SetParent;
        public delegate* unmanaged<ulong, ulong*, int, int> GetChildren;
        public delegate* unmanaged<ulong, int, float*, void> GetTransform;
        public delegate* unmanaged<ulong, int, float*, void> SetTransform;

        public delegate* unmanaged<byte*, ulong, ulong> CreateEntity;
        public delegate* unmanaged<byte*, float*, ulong> Spawn;
        public delegate* unmanaged<ulong, ulong> Duplicate;
        public delegate* unmanaged<ulong, void> Destroy;

        public delegate* unmanaged<ulong, int, int> HasComponent;
        public delegate* unmanaged<ulong, int, int> AddComponent;
        public delegate* unmanaged<ulong, int, void> RemoveComponent;
        public delegate* unmanaged<ulong, int, float*, int> GetProperty;
        public delegate* unmanaged<ulong, int, float*, int> SetProperty;
        public delegate* unmanaged<ulong, float*, float*, void> AddImpulse;
        public delegate* unmanaged<ulong, byte*, int> AddScript;
        public delegate* unmanaged<ulong, float*, void> SetNavTarget;

        public delegate* unmanaged<float*, float*, float, ulong, ulong*, float*, int> Raycast;

        public delegate* unmanaged<int, int, int> GetKey;
        public delegate* unmanaged<int, int, int> GetMouseButton;
        public delegate* unmanaged<float*, void> GetMouse;
        public delegate* unmanaged<double*, void> GetTime;
        public delegate* unmanaged<ulong> GetGameCamera;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ManagedApi
    {
        public delegate* unmanaged<NativeApi*, byte*, int> Initialize;
        public delegate* unmanaged<int> Reload;
        public delegate* unmanaged<ulong, byte*, byte*, long> CreateInstance;
        public delegate* unmanaged<long, void> DestroyInstance;
        public delegate* unmanaged<float, void> Update;
        public delegate* unmanaged<void> StopAll;
        public delegate* unmanaged<byte*, int, int> GetInfo;
    }

    // Component types and properties (ScriptApi.h)
    public enum PropertyId
    {
        PhysicsVelocity = 0,
        PhysicsAngularVelocity = 1,
        PhysicsMass = 2,
        NavTarget = 3,
        NavSpeed = 4,
        NavArrived = 5,
        NavHasTarget = 6,
        LightColor = 7,
        LightIntensity = 8,
        LightEnabled = 9,
        MeshVisible = 10,
        CameraFov = 11,
        VolumeDensity = 12,
        VolumeColor = 13,
        VolumeGlow = 14,
    }

    internal static unsafe class Native
    {
        internal static NativeApi* Api;

        internal static byte[] Utf8(string text) => System.Text.Encoding.UTF8.GetBytes((text ?? "") + "\0");

        internal static void Log(int level, string message)
        {
            if (Api == null)
            {
                System.Console.WriteLine(message);
                return;
            }
            fixed (byte* p = Utf8(message))
            {
                Api->Log(level, p);
            }
        }
    }
}
