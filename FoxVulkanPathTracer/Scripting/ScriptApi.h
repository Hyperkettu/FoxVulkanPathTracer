#pragma once

#include <cstdint>

namespace Fox {

	namespace Scripting {

		// The contract between the engine and the C# scripting host (Scripting/FoxScripting, Native.cs mirrors this):
		// the order of the fields is what matters. Strings are UTF-8 and zero terminated; vectors are float arrays.
		//
		// Entities are 64 bit ids: generation << 32 | (index + 1), so 0 is "none".

		enum class ComponentType : int32_t { Mesh = 0, Light, Physics, NavAgent, Volume, Camera, Script, Water, Sound };

		enum class Property : int32_t {
			PhysicsVelocity = 0,     // vec3, read / write (playing)
			PhysicsAngularVelocity,  // vec3, read / write (playing)
			PhysicsMass,             // read (playing)
			NavTarget,               // vec3, read (write with SetNavTarget)
			NavSpeed,
			NavArrived,              // read
			NavHasTarget,            // read; write 0: no target
			LightColor,              // vec3
			LightIntensity,
			LightEnabled,
			MeshVisible,
			CameraFov,               // degrees
			VolumeDensity,
			VolumeColor,             // vec3
			VolumeGlow,
			PhysicsLayer,            // read / write (playing: moves the body to it)
			PhysicsTrigger,          // read
			SoundVolume,             // The entity's Sound component (applies to it playing too)
			SoundPitch,
			SoundLoop,
			SoundFile,               // Not a number: see SetSoundFile
		};

		// Contact events to scripts (ManagedApi::Contact)
		enum class ContactKind : int32_t { CollisionEnter = 0, CollisionExit, TriggerEnter, TriggerExit };

		// Shapes for casts and overlaps: size4 = radius (sphere), half extents (box), radius and half height (capsule, along y)
		enum class QueryShapeType : int32_t { Sphere = 0, Box, Capsule };

		// The engine's functions (ScriptBindings in MainRayTracingSceneScripting.cpp)
		struct NativeApi {
			void (*Log)(int32_t level, const char* message); // 0 info, 1 warning, 2 error

			uint64_t (*FindEntity)(const char* name);
			int32_t (*IsValid)(uint64_t entity);
			int32_t (*GetName)(uint64_t entity, char* buffer, int32_t capacity); // Length (bytes, without the zero)
			void (*SetName)(uint64_t entity, const char* name);
			int32_t (*GetActive)(uint64_t entity);
			void (*SetActive)(uint64_t entity, int32_t active);
			uint64_t (*GetParent)(uint64_t entity);
			void (*SetParent)(uint64_t entity, uint64_t parent);  // Keeps its world transform
			int32_t (*GetChildren)(uint64_t entity, uint64_t* children, int32_t capacity); // How many there are
			// Position (3), rotation (quaternion x y z w), scale (3): world (1) or relative to the parent (0)
			void (*GetTransform)(uint64_t entity, int32_t world, float* transform10);
			void (*SetTransform)(uint64_t entity, int32_t world, const float* transform10);

			uint64_t (*CreateEntity)(const char* name, uint64_t parent);
			uint64_t (*Spawn)(const char* what, const float* position3);
			uint64_t (*Duplicate)(uint64_t entity);
			void (*Destroy)(uint64_t entity);

			int32_t (*HasComponent)(uint64_t entity, int32_t type);
			int32_t (*AddComponent)(uint64_t entity, int32_t type);
			void (*RemoveComponent)(uint64_t entity, int32_t type);
			int32_t (*GetProperty)(uint64_t entity, int32_t property, float* value4);
			int32_t (*SetProperty)(uint64_t entity, int32_t property, const float* value4);
			void (*AddImpulse)(uint64_t entity, const float* linear3, const float* angular3); // Either may be null
			int32_t (*AddScript)(uint64_t entity, const char* className);
			void (*SetNavTarget)(uint64_t entity, const float* target3);

			// The first physics body or ground along the ray: hit7 = point (3), normal (3), distance
			int32_t (*Raycast)(const float* origin3, const float* direction3, float maxDistance, uint64_t ignore, uint64_t* entity, float* hit7);

			int32_t (*GetKey)(int32_t scancode, int32_t mode);   // mode 0: held, 1: pressed this frame, 2: released this frame
			int32_t (*GetMouseButton)(int32_t button, int32_t mode); // 0 left, 1 middle, 2 right
			void (*GetMouse)(float* value4);                     // Delta x, y this frame; position x, y
			void (*GetTime)(double* value4);                     // Game time, delta, time scale, frame
			uint64_t (*GetGameCamera)();

			// Queries (physics bodies while playing; the ground only otherwise). Hits are point (3), normal (3), distance;
			// entity 0 for the ground. They return how many there were (filling up to capacity, nearest first).
			int32_t (*RaycastAll)(const float* origin3, const float* direction3, float maxDistance, uint32_t layerMask, uint64_t ignore,
				int32_t triggers, uint64_t* entities, float* hits7, int32_t capacity);
			int32_t (*ShapeCast)(int32_t shape, const float* size4, const float* origin3, const float* rotation4, const float* direction3,
				float maxDistance, uint32_t layerMask, uint64_t ignore, int32_t triggers, uint64_t* entity, float* hit7);
			int32_t (*Overlap)(int32_t shape, const float* size4, const float* center3, const float* rotation4, uint32_t layerMask,
				uint64_t ignore, int32_t triggers, uint64_t* entities, int32_t capacity);
			int32_t (*GetLayerName)(int32_t layer, char* buffer, int32_t capacity);
			int32_t (*LayerFromName)(const char* name); // -1: none of that name

			// Named input (Settings/Input.json): an action held / pressed / released (mode as GetKey), an axis -1..1
			int32_t (*GetAction)(const char* name, int32_t mode);
			float (*GetAxis)(const char* name);

			// Sound (Audio/AudioEngine.h). Handles are 0 when nothing plays.
			int64_t (*PlaySound)(const char* file, int32_t spatial, const float* position3, float volume, float pitch, int32_t loop,
				float minDistance, float maxDistance);
			void (*StopSound)(int64_t sound, float fadeSeconds);
			int32_t (*SoundIsPlaying)(int64_t sound);
			void (*SetSoundParameter)(int64_t sound, int32_t parameter, const float* value3); // 0 volume, 1 pitch, 2 position
			int64_t (*PlayMusic)(const char* file, int32_t loop, float volume, float fadeSeconds);
			void (*StopMusic)(float fadeSeconds);
			float (*GetBusVolume)(int32_t bus);                  // 0 master, 1 music, 2 effects
			void (*SetBusVolume)(int32_t bus, float volume);
			int32_t (*EntitySound)(uint64_t entity, int32_t action); // 0 play, 1 stop, 2 is it playing
			void (*SetSoundFile)(uint64_t entity, const char* file);
			void (*SetText)(uint64_t entity, const char* text); // TextComponent (UI or world)
		};

		// The scripting host's functions (Host.cs; Bootstrap fills them in)
		struct ManagedApi {
			int32_t (*Initialize)(const NativeApi* api, const char* scriptsDirectory); // Compiles the scripts
			int32_t (*Reload)();                                  // Compiles them again; running ones carry over
			int64_t (*CreateInstance)(uint64_t entity, const char* className, const char* fieldsJson); // 0: failed
			void (*DestroyInstance)(int64_t instance);
			void (*Update)(float deltaTime);
			void (*StopAll)();
			int32_t (*GetInfo)(char* buffer, int32_t capacity);  // JSON of classes, fields and errors (see Host.GetInfo)
			// A contact or trigger began or ended for the entity (ContactKind): data7 = point, normal (away from other), speed
			void (*Contact)(uint64_t entity, int32_t kind, uint64_t other, const float* data7);
		};
	}
}
