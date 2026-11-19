// The API C# scripts see: Script (derive from it), Entity and its components, Scene, Input, Time, Debug.
// Vectors and rotations are System.Numerics (Vector3, Quaternion). Metres, seconds, degrees where it says so.

using System;
using System.Collections.Generic;
using System.Numerics;
using Fox.Interop;

namespace Fox
{
    /// <summary>
    /// A script: derive from it, put the file in Scripts/, and add it to an entity (Inspector > Add Component >
    /// Script). While the game plays (F5) it gets OnStart once, then OnUpdate every frame of game time. Its public
    /// fields (float, int, bool, string, Vector3) show in the Inspector and are saved with the scene. Saving the file
    /// reloads it, also while playing: fields keep their values and OnReload is called.
    /// </summary>
    public abstract class Script
    {
        /// <summary>The entity it is on (a field, so Entity.Position = ... works)</summary>
        public Entity Entity;

        /// <summary>Before its first update (when play starts, or when it is added while playing)</summary>
        public virtual void OnStart() { }

        /// <summary>Every frame of game time (not while paused); deltaTime in seconds, scaled by the game speed</summary>
        public virtual void OnUpdate(float deltaTime) { }

        /// <summary>Play stopped, or its entity or script was removed</summary>
        public virtual void OnStop() { }

        /// <summary>Its file was saved and reloaded while playing (fields were carried over)</summary>
        public virtual void OnReload() { }

        /// <summary>Its body started touching another (or the ground: Other is None). Needs a Physics component.</summary>
        public virtual void OnCollisionEnter(Collision collision) { }
        /// <summary>It stopped touching it (only Other is set)</summary>
        public virtual void OnCollisionExit(Collision collision) { }
        /// <summary>Something entered its trigger, or it entered a trigger (either one is a trigger body)</summary>
        public virtual void OnTriggerEnter(Entity other) { }
        public virtual void OnTriggerExit(Entity other) { }

        // Shortcuts to its entity
        public Vector3 Position { get => Entity.Position; set => Entity.Position = value; }
        public Quaternion Rotation { get => Entity.Rotation; set => Entity.Rotation = value; }
    }

    public enum ComponentType
    {
        Mesh = 0,
        Light = 1,
        Physics = 2,
        NavAgent = 3,
        Volume = 4,
        Camera = 5,
        Script = 6,
        Water = 7,
    }

    /// <summary>SDL scancodes: where the key is on a US layout</summary>
    public enum Key
    {
        A = 4, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        D1 = 30, D2, D3, D4, D5, D6, D7, D8, D9, D0,
        Return = 40, Escape = 41, Backspace = 42, Tab = 43, Space = 44,
        Right = 79, Left = 80, Down = 81, Up = 82,
        LeftControl = 224, LeftShift = 225, LeftAlt = 226, RightControl = 228, RightShift = 229, RightAlt = 230,
    }

    /// <summary>A contact: what it touched, where, which way it is pushed (away from Other) and how hard they met</summary>
    public struct Collision
    {
        public Entity Other;
        public Vector3 Point;
        public Vector3 Normal;
        public float Speed;     // m/s along the normal
    }

    public struct RaycastHit
    {
        public Entity Entity;   // What it hit (None for the ground)
        public Vector3 Point;
        public Vector3 Normal;
        public float Distance;
    }

    /// <summary>An entity of the scene (a handle: it stops being valid when the entity is destroyed). default is None.</summary>
    public readonly unsafe struct Entity : IEquatable<Entity>
    {
        public readonly ulong Id;
        public static readonly Entity None = default;

        public Entity(ulong id) { Id = id; }

        public bool IsValid => Id != 0 && Native.Api != null && Native.Api->IsValid(Id) != 0;
        public static implicit operator bool(Entity entity) => entity.IsValid;

        public string Name
        {
            get
            {
                byte* buffer = stackalloc byte[256];
                int length = Native.Api->GetName(Id, buffer, 256);
                return System.Text.Encoding.UTF8.GetString(buffer, Math.Min(length, 255));
            }
            set { fixed (byte* p = Native.Utf8(value)) Native.Api->SetName(Id, p); }
        }

        /// <summary>Whether it (and everything under it) is in the scene; inactive entities are hidden and do nothing</summary>
        public bool Active
        {
            get => Native.Api->GetActive(Id) != 0;
            set => Native.Api->SetActive(Id, value ? 1 : 0);
        }

        public Entity Parent
        {
            get => new Entity(Native.Api->GetParent(Id));
            set => Native.Api->SetParent(Id, value.Id);
        }

        public Entity[] Children
        {
            get
            {
                int count = Native.Api->GetChildren(Id, null, 0);
                var ids = new ulong[count];
                fixed (ulong* p = ids) Native.Api->GetChildren(Id, p, count);
                return Array.ConvertAll(ids, id => new Entity(id));
            }
        }

        // --- Transform ---

        private void Get(bool world, out Vector3 position, out Quaternion rotation, out Vector3 scale)
        {
            float* t = stackalloc float[10];
            Native.Api->GetTransform(Id, world ? 1 : 0, t);
            position = new Vector3(t[0], t[1], t[2]);
            rotation = new Quaternion(t[3], t[4], t[5], t[6]);
            scale = new Vector3(t[7], t[8], t[9]);
        }

        private void Set(bool world, Vector3 position, Quaternion rotation, Vector3 scale)
        {
            float* t = stackalloc float[10] { position.X, position.Y, position.Z, rotation.X, rotation.Y, rotation.Z, rotation.W, scale.X, scale.Y, scale.Z };
            Native.Api->SetTransform(Id, world ? 1 : 0, t);
        }

        /// <summary>World position</summary>
        public Vector3 Position
        {
            get { Get(true, out var p, out _, out _); return p; }
            set { Get(true, out _, out var r, out var s); Set(true, value, r, s); }
        }

        /// <summary>World rotation</summary>
        public Quaternion Rotation
        {
            get { Get(true, out _, out var r, out _); return r; }
            set { Get(true, out var p, out _, out var s); Set(true, p, value, s); }
        }

        /// <summary>Scale relative to its parent</summary>
        public Vector3 Scale
        {
            get { Get(false, out _, out _, out var s); return s; }
            set { Get(false, out var p, out var r, out _); Set(false, p, r, value); }
        }

        public Vector3 LocalPosition
        {
            get { Get(false, out var p, out _, out _); return p; }
            set { Get(false, out _, out var r, out var s); Set(false, value, r, s); }
        }

        public Quaternion LocalRotation
        {
            get { Get(false, out _, out var r, out _); return r; }
            set { Get(false, out var p, out _, out var s); Set(false, p, value, s); }
        }

        /// <summary>Its +Z axis in the world: the front of glTF models and of agents. Cameras and lights look along -Forward.</summary>
        public Vector3 Forward => Vector3.Transform(Vector3.UnitZ, Rotation);
        public Vector3 Up => Vector3.Transform(Vector3.UnitY, Rotation);
        /// <summary>Its +X axis in the world</summary>
        public Vector3 Right => Vector3.Transform(Vector3.UnitX, Rotation);

        public void Translate(Vector3 offset) => Position += offset;

        /// <summary>Turns it about a world axis</summary>
        public void Rotate(Vector3 axis, float degrees) =>
            Rotation = Quaternion.Normalize(Quaternion.CreateFromAxisAngle(Vector3.Normalize(axis), degrees * MathF.PI / 180.0f) * Rotation);

        /// <summary>Turns it so its Forward (+Z) points at a world point (staying upright)</summary>
        public void LookAt(Vector3 target)
        {
            Vector3 direction = target - Position;
            if (direction.LengthSquared() < 1e-12f) return;
            float yaw = MathF.Atan2(direction.X, direction.Z);
            float pitch = -MathF.Asin(Math.Clamp(Vector3.Normalize(direction).Y, -1.0f, 1.0f));
            Rotation = Quaternion.CreateFromYawPitchRoll(yaw, pitch, 0.0f);
        }

        /// <summary>Turns it so its -Z (where cameras and lights look) points at a world point (staying upright)</summary>
        public void AimAt(Vector3 target) => LookAt(Position - (target - Position));

        // --- Life ---

        /// <summary>Removes it and everything under it from the scene (it comes back when play stops)</summary>
        public void Destroy() => Native.Api->Destroy(Id);

        /// <summary>A copy of it and everything under it (meshes in it make the scene rebuild once: keep a few hidden and show them instead when it matters)</summary>
        public Entity Duplicate() => new Entity(Native.Api->Duplicate(Id));

        // --- Components ---

        public bool Has(ComponentType type) => Native.Api->HasComponent(Id, (int)type) != 0;
        public bool Add(ComponentType type) => Native.Api->AddComponent(Id, (int)type) != 0;
        public void Remove(ComponentType type) => Native.Api->RemoveComponent(Id, (int)type);

        public PhysicsBody Physics => new PhysicsBody(this);
        public NavAgent Agent => new NavAgent(this);
        public Light Light => new Light(this);
        public MeshView Mesh => new MeshView(this);
        public CameraView Camera => new CameraView(this);
        public VolumeView Volume => new VolumeView(this);

        /// <summary>A script of this class on it (null: none)</summary>
        public T GetScript<T>() where T : Script => Host.FindScript<T>(this);

        /// <summary>Adds a script by its class name (it starts before its first update)</summary>
        public bool AddScript(string className) { fixed (byte* p = Native.Utf8(className)) return Native.Api->AddScript(Id, p) != 0; }
        public bool AddScript<T>() where T : Script => AddScript(typeof(T).Name);

        public static Entity Find(string name) { fixed (byte* p = Native.Utf8(name)) return new Entity(Native.Api->FindEntity(p)); }
        public static Entity Create(string name, Entity parent = default)
        {
            fixed (byte* p = Native.Utf8(name)) return new Entity(Native.Api->CreateEntity(p, parent.Id));
        }

        internal Vector4 GetProperty(PropertyId property)
        {
            float* v = stackalloc float[4];
            Native.Api->GetProperty(Id, (int)property, v);
            return new Vector4(v[0], v[1], v[2], v[3]);
        }

        internal void SetProperty(PropertyId property, Vector4 value)
        {
            float* v = stackalloc float[4] { value.X, value.Y, value.Z, value.W };
            Native.Api->SetProperty(Id, (int)property, v);
        }

        internal static Vector3 Xyz(Vector4 v) => new Vector3(v.X, v.Y, v.Z);
        internal static Vector4 W(Vector3 v) => new Vector4(v, 0.0f);
        internal static Vector4 W(float v) => new Vector4(v, 0.0f, 0.0f, 0.0f);

        public bool Equals(Entity other) => Id == other.Id;
        public override bool Equals(object obj) => obj is Entity other && Equals(other);
        public override int GetHashCode() => Id.GetHashCode();
        public static bool operator ==(Entity a, Entity b) => a.Id == b.Id;
        public static bool operator !=(Entity a, Entity b) => a.Id != b.Id;
        public override string ToString() => IsValid ? Name : "(none)";
    }

    /// <summary>Its rigid body (Physics component) while the game plays</summary>
    public sealed unsafe class PhysicsBody
    {
        private readonly Entity entity;
        internal PhysicsBody(Entity entity) { this.entity = entity; }
        public bool Exists => entity.Has(ComponentType.Physics);

        public Vector3 Velocity
        {
            get => Entity.Xyz(entity.GetProperty(PropertyId.PhysicsVelocity));
            set => entity.SetProperty(PropertyId.PhysicsVelocity, Entity.W(value));
        }
        /// <summary>Radians per second about each world axis</summary>
        public Vector3 AngularVelocity
        {
            get => Entity.Xyz(entity.GetProperty(PropertyId.PhysicsAngularVelocity));
            set => entity.SetProperty(PropertyId.PhysicsAngularVelocity, Entity.W(value));
        }
        public float Mass => entity.GetProperty(PropertyId.PhysicsMass).X;

        /// <summary>Its collision layer (0..15, see Layers); can change while playing</summary>
        public int Layer
        {
            get => (int)entity.GetProperty(PropertyId.PhysicsLayer).X;
            set => entity.SetProperty(PropertyId.PhysicsLayer, Entity.W((float)value));
        }
        /// <summary>A trigger: collides with nothing, reports what enters and leaves it</summary>
        public bool IsTrigger => entity.GetProperty(PropertyId.PhysicsTrigger).X != 0.0f;

        /// <summary>A push (kg m/s) at its centre: changes its velocity by impulse / mass at once</summary>
        public void AddImpulse(Vector3 impulse)
        {
            float* linear = stackalloc float[3] { impulse.X, impulse.Y, impulse.Z };
            Native.Api->AddImpulse(entity.Id, linear, null);
        }
        public void AddAngularImpulse(Vector3 impulse)
        {
            float* angular = stackalloc float[3] { impulse.X, impulse.Y, impulse.Z };
            Native.Api->AddImpulse(entity.Id, null, angular);
        }
        /// <summary>A force (newtons) over this frame</summary>
        public void AddForce(Vector3 force) => AddImpulse(force * Time.Delta);
    }

    /// <summary>Its Nav Agent: walks over the ground to a target on a path found with A* (while the game plays)</summary>
    public sealed unsafe class NavAgent
    {
        private readonly Entity entity;
        internal NavAgent(Entity entity) { this.entity = entity; }
        public bool Exists => entity.Has(ComponentType.NavAgent);

        /// <summary>Where it walks to; setting it plans the way there</summary>
        public Vector3 Target
        {
            get => Entity.Xyz(entity.GetProperty(PropertyId.NavTarget));
            set { float* t = stackalloc float[3] { value.X, value.Y, value.Z }; Native.Api->SetNavTarget(entity.Id, t); }
        }
        public bool HasTarget => entity.GetProperty(PropertyId.NavHasTarget).X != 0.0f;
        public bool Arrived => entity.GetProperty(PropertyId.NavArrived).X != 0.0f;
        public float Speed
        {
            get => entity.GetProperty(PropertyId.NavSpeed).X;
            set => entity.SetProperty(PropertyId.NavSpeed, Entity.W(value));
        }
        /// <summary>Stops walking (no target)</summary>
        public void Stop() => entity.SetProperty(PropertyId.NavHasTarget, Entity.W(0.0f));
    }

    public sealed unsafe class Light
    {
        private readonly Entity entity;
        internal Light(Entity entity) { this.entity = entity; }
        public bool Exists => entity.Has(ComponentType.Light);
        public Vector3 Color { get => Entity.Xyz(entity.GetProperty(PropertyId.LightColor)); set => entity.SetProperty(PropertyId.LightColor, Entity.W(value)); }
        public float Intensity { get => entity.GetProperty(PropertyId.LightIntensity).X; set => entity.SetProperty(PropertyId.LightIntensity, Entity.W(value)); }
        public bool Enabled { get => entity.GetProperty(PropertyId.LightEnabled).X != 0.0f; set => entity.SetProperty(PropertyId.LightEnabled, Entity.W(value ? 1.0f : 0.0f)); }
    }

    public sealed unsafe class MeshView
    {
        private readonly Entity entity;
        internal MeshView(Entity entity) { this.entity = entity; }
        public bool Exists => entity.Has(ComponentType.Mesh);
        /// <summary>Shown or hidden, at once (no rebuild)</summary>
        public bool Visible { get => entity.GetProperty(PropertyId.MeshVisible).X != 0.0f; set => entity.SetProperty(PropertyId.MeshVisible, Entity.W(value ? 1.0f : 0.0f)); }
    }

    public sealed unsafe class CameraView
    {
        private readonly Entity entity;
        internal CameraView(Entity entity) { this.entity = entity; }
        public bool Exists => entity.Has(ComponentType.Camera);
        /// <summary>Vertical field of view, degrees</summary>
        public float FieldOfView { get => entity.GetProperty(PropertyId.CameraFov).X; set => entity.SetProperty(PropertyId.CameraFov, Entity.W(value)); }
    }

    public sealed unsafe class VolumeView
    {
        private readonly Entity entity;
        internal VolumeView(Entity entity) { this.entity = entity; }
        public bool Exists => entity.Has(ComponentType.Volume);
        public float Density { get => entity.GetProperty(PropertyId.VolumeDensity).X; set => entity.SetProperty(PropertyId.VolumeDensity, Entity.W(value)); }
        public Vector3 Color { get => Entity.Xyz(entity.GetProperty(PropertyId.VolumeColor)); set => entity.SetProperty(PropertyId.VolumeColor, Entity.W(value)); }
        public float Glow { get => entity.GetProperty(PropertyId.VolumeGlow).X; set => entity.SetProperty(PropertyId.VolumeGlow, Entity.W(value)); }
    }

    public static unsafe class Scene
    {
        /// <summary>
        /// A new entity at position: "ball" and "box" (with a physics body), "sphere", "smoke", "glowing smoke",
        /// "agent", or a model file ("Models/cabin.glb"). Meshes make the scene rebuild once (a short pause): for
        /// things that appear often, keep hidden ones ready and show them (Mesh.Visible, Active).
        /// </summary>
        public static Entity Spawn(string what, Vector3 position)
        {
            float* p = stackalloc float[3] { position.X, position.Y, position.Z };
            fixed (byte* w = Native.Utf8(what)) return new Entity(Native.Api->Spawn(w, p));
        }

        public static Entity Find(string name) => Entity.Find(name);

        /// <summary>The first physics body (or the ground) along a ray, within maxDistance; ignore: an entity to see through (e.g. yourself). See Physics for more.</summary>
        public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit, Entity ignore = default) =>
            Physics.Raycast(origin, direction, maxDistance, out hit, Layers.All, ignore);

        /// <summary>The entity the game looks through (its Camera component)</summary>
        public static Entity GameCamera => new Entity(Native.Api->GetGameCamera());
    }

    /// <summary>Collision layers: 16, named in the Environment window, where a matrix says which collide</summary>
    public static unsafe class Layers
    {
        public const uint All = 0xffff;

        /// <summary>The layer of this name (-1: none)</summary>
        public static int NameToLayer(string name) { fixed (byte* p = Native.Utf8(name)) return Native.Api->LayerFromName(p); }

        public static string LayerToName(int layer)
        {
            byte* buffer = stackalloc byte[128];
            int length = Native.Api->GetLayerName(layer, buffer, 128);
            return System.Text.Encoding.UTF8.GetString(buffer, Math.Min(length, 127));
        }

        /// <summary>A mask of the named layers, for queries: Layers.Mask("Ground", "Enemy")</summary>
        public static uint Mask(params string[] names)
        {
            uint mask = 0;
            foreach (string name in names)
            {
                int layer = NameToLayer(name);
                if (layer >= 0) mask |= 1u << layer;
                else Debug.Warn($"Layers.Mask: no layer called \"{name}\"");
            }
            return mask;
        }
    }

    /// <summary>
    /// Physics queries, against the bodies while the game plays (and the ground). layerMask: the layers to hit (Layers.Mask);
    /// ignore: an entity to pass through (usually yourself); hitTriggers: whether triggers count.
    /// </summary>
    public static unsafe class Physics
    {
        private const int MaxHits = 256;

        private static RaycastHit Hit(ulong entity, float* h) => new RaycastHit
        {
            Entity = new Entity(entity),
            Point = new Vector3(h[0], h[1], h[2]),
            Normal = new Vector3(h[3], h[4], h[5]),
            Distance = h[6],
        };

        public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit,
            uint layerMask = Layers.All, Entity ignore = default, bool hitTriggers = false)
        {
            hit = default;
            if (direction.LengthSquared() < 1e-12f) return false;
            direction = Vector3.Normalize(direction);
            float* o = stackalloc float[3] { origin.X, origin.Y, origin.Z };
            float* d = stackalloc float[3] { direction.X, direction.Y, direction.Z };
            float* h = stackalloc float[7];
            ulong entity;
            if (Native.Api->RaycastAll(o, d, maxDistance, layerMask, ignore.Id, hitTriggers ? 1 : 0, &entity, h, 1) == 0) return false;
            hit = Hit(entity, h);
            return true;
        }

        /// <summary>Everything along the ray, nearest first (one hit per body)</summary>
        public static RaycastHit[] RaycastAll(Vector3 origin, Vector3 direction, float maxDistance,
            uint layerMask = Layers.All, Entity ignore = default, bool hitTriggers = false)
        {
            if (direction.LengthSquared() < 1e-12f) return Array.Empty<RaycastHit>();
            direction = Vector3.Normalize(direction);
            float* o = stackalloc float[3] { origin.X, origin.Y, origin.Z };
            float* d = stackalloc float[3] { direction.X, direction.Y, direction.Z };
            var entities = new ulong[MaxHits];
            var hits = new float[MaxHits * 7];
            int count;
            fixed (ulong* e = entities)
            fixed (float* h = hits)
            {
                count = Math.Min(Native.Api->RaycastAll(o, d, maxDistance, layerMask, ignore.Id, hitTriggers ? 1 : 0, e, h, MaxHits), MaxHits);
                var result = new RaycastHit[count];
                for (int i = 0; i < count; i++) result[i] = Hit(entities[i], h + i * 7);
                return result;
            }
        }

        private static bool Cast(int shape, Vector4 size, Vector3 origin, Quaternion rotation, Vector3 direction, float maxDistance,
            out RaycastHit hit, uint layerMask, Entity ignore, bool hitTriggers)
        {
            hit = default;
            if (direction.LengthSquared() < 1e-12f) return false;
            direction = Vector3.Normalize(direction);
            float* s = stackalloc float[4] { size.X, size.Y, size.Z, size.W };
            float* o = stackalloc float[3] { origin.X, origin.Y, origin.Z };
            float* r = stackalloc float[4] { rotation.X, rotation.Y, rotation.Z, rotation.W };
            float* d = stackalloc float[3] { direction.X, direction.Y, direction.Z };
            float* h = stackalloc float[7];
            ulong entity;
            if (Native.Api->ShapeCast(shape, s, o, r, d, maxDistance, layerMask, ignore.Id, hitTriggers ? 1 : 0, &entity, h) == 0) return false;
            hit = Hit(entity, h);
            return true;
        }

        /// <summary>A sphere swept along a direction: the first thing it touches (Distance: how far its centre got)</summary>
        public static bool SphereCast(Vector3 origin, float radius, Vector3 direction, float maxDistance, out RaycastHit hit,
            uint layerMask = Layers.All, Entity ignore = default, bool hitTriggers = false) =>
            Cast(0, new Vector4(radius, 0, 0, 0), origin, Quaternion.Identity, direction, maxDistance, out hit, layerMask, ignore, hitTriggers);

        public static bool BoxCast(Vector3 center, Vector3 halfExtents, Quaternion rotation, Vector3 direction, float maxDistance, out RaycastHit hit,
            uint layerMask = Layers.All, Entity ignore = default, bool hitTriggers = false) =>
            Cast(1, new Vector4(halfExtents, 0), center, rotation, direction, maxDistance, out hit, layerMask, ignore, hitTriggers);

        /// <summary>A capsule (along its y: halfHeight is half its straight part) swept along a direction</summary>
        public static bool CapsuleCast(Vector3 center, float radius, float halfHeight, Quaternion rotation, Vector3 direction, float maxDistance,
            out RaycastHit hit, uint layerMask = Layers.All, Entity ignore = default, bool hitTriggers = false) =>
            Cast(2, new Vector4(radius, halfHeight, 0, 0), center, rotation, direction, maxDistance, out hit, layerMask, ignore, hitTriggers);

        private static Entity[] Overlap(int shape, Vector4 size, Vector3 center, Quaternion rotation, uint layerMask, Entity ignore, bool hitTriggers)
        {
            float* s = stackalloc float[4] { size.X, size.Y, size.Z, size.W };
            float* c = stackalloc float[3] { center.X, center.Y, center.Z };
            float* r = stackalloc float[4] { rotation.X, rotation.Y, rotation.Z, rotation.W };
            var entities = new ulong[MaxHits];
            fixed (ulong* e = entities)
            {
                int count = Math.Min(Native.Api->Overlap(shape, s, c, r, layerMask, ignore.Id, hitTriggers ? 1 : 0, e, MaxHits), MaxHits);
                var result = new Entity[count];
                for (int i = 0; i < count; i++) result[i] = new Entity(entities[i]);
                return result;
            }
        }

        /// <summary>The entities whose bodies are inside or touch the sphere (not the ground)</summary>
        public static Entity[] OverlapSphere(Vector3 center, float radius, uint layerMask = Layers.All, Entity ignore = default, bool hitTriggers = false) =>
            Overlap(0, new Vector4(radius, 0, 0, 0), center, Quaternion.Identity, layerMask, ignore, hitTriggers);

        public static Entity[] OverlapBox(Vector3 center, Vector3 halfExtents, Quaternion rotation, uint layerMask = Layers.All, Entity ignore = default, bool hitTriggers = false) =>
            Overlap(1, new Vector4(halfExtents, 0), center, rotation, layerMask, ignore, hitTriggers);

        public static Entity[] OverlapCapsule(Vector3 center, float radius, float halfHeight, Quaternion rotation, uint layerMask = Layers.All,
            Entity ignore = default, bool hitTriggers = false) =>
            Overlap(2, new Vector4(radius, halfHeight, 0, 0), center, rotation, layerMask, ignore, hitTriggers);
    }

    public static unsafe class Input
    {
        public static bool GetKey(Key key) => Native.Api->GetKey((int)key, 0) != 0;
        /// <summary>Pressed this frame</summary>
        public static bool GetKeyDown(Key key) => Native.Api->GetKey((int)key, 1) != 0;
        /// <summary>Released this frame</summary>
        public static bool GetKeyUp(Key key) => Native.Api->GetKey((int)key, 2) != 0;
        /// <summary>0 left, 1 middle, 2 right</summary>
        public static bool GetMouseButton(int button) => Native.Api->GetMouseButton(button, 0) != 0;
        public static bool GetMouseButtonDown(int button) => Native.Api->GetMouseButton(button, 1) != 0;
        public static bool GetMouseButtonUp(int button) => Native.Api->GetMouseButton(button, 2) != 0;

        /// <summary>How far the mouse moved this frame, pixels (also while it is captured outside edit mode)</summary>
        public static Vector2 MouseDelta { get { float* m = stackalloc float[4]; Native.Api->GetMouse(m); return new Vector2(m[0], m[1]); } }
        /// <summary>Where the pointer is in the window, pixels</summary>
        public static Vector2 MousePosition { get { float* m = stackalloc float[4]; Native.Api->GetMouse(m); return new Vector2(m[2], m[3]); } }
    }

    public static unsafe class Time
    {
        private static double Get(int i) { double* t = stackalloc double[4]; Native.Api->GetTime(t); return t[i]; }
        /// <summary>Game seconds since play started</summary>
        public static double Elapsed => Get(0);
        /// <summary>This frame's step of game time, seconds</summary>
        public static float Delta => (float)Get(1);
        /// <summary>The game's speed (1: real time)</summary>
        public static float Scale => (float)Get(2);
        public static long Frame => (long)Get(3);
    }

    public static class Debug
    {
        public static void Log(object message) => Native.Log(0, message?.ToString() ?? "null");
        public static void Warn(object message) => Native.Log(1, message?.ToString() ?? "null");
        public static void Error(object message) => Native.Log(2, message?.ToString() ?? "null");
    }
}
