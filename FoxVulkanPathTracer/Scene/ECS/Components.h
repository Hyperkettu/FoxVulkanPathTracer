#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

#include "FoxVulkanPathTracer/Scene/ECS/Entity.h"

// The components of scene entities.
//
// Every entity of a SceneGraph has a NameComponent, a HierarchyComponent and a WorldTransformComponent (kept by
// the graph), and a TransformComponent unless it was removed. The rest describe what the entity is:
//
//   TransformComponent  where it is, relative to its parent (on by default)
//   MeshComponent       it is rendered: a glTF model, a sphere or a generated tree
//   LightComponent      it gives light: a directional light (the sun), a point light or a spot light
//   WaterComponent      it is a body of water, simulated (shallow water equations) and refracting
//   PhysicsComponent    it is a rigid body (Jolt Physics, Scene/Physics.h): it falls, collides and floats in play mode
//   NavAgentComponent   it walks over the ground to a target, on a path found with A* (Scene/Navigation.h)
//   VolumeComponent     it is a volume of smoke (or glowing haze) that light scatters in (Shaders/volumes.slang)
//   CameraComponent     the game looks through it while it plays (it looks along its -Z axis)
//   ScriptComponent     C# scripts run on it while the game plays (Scripts/*.cs, see Scripting/ScriptHost.h)
//   PrefabComponent     it is an instance of a prefab (Prefabs/*.prefab.json): what is under it comes from there
//   SoundComponent      it plays a sound (in the world, from where it is) or background music (Audio/AudioEngine.h)
//   ClutterComponent    it scatters copies of a mesh over the ground around it: grass, flowers, stones (instanced)
//   ParticleComponent   it emits particles: fire, magic sparks or leaves flying in the wind
//   TextComponent       vector text: on the screen (the game's UI) or in the world, flat, facing the camera or 3D
//   SpriteComponent     an image on the screen (the game's UI)
//   UIAnimationComponent animates the entity's UI element (sprite or screen text): steps in sequence, with easings
//   UIButtonComponent   makes the entity's UI element a button: hover and press feedback, plays animations on a click
//   UICanvasComponent   a UI in the world: the UI elements under it drawn onto a panel in the scene (fixed or billboard)
//
// Adding a component type (e.g. audio):
//   1. the struct here
//   2. its inspector entry: COMPONENT_EDITORS in Editor/SceneGraphEditor.cpp
//   3. how it is saved: COMPONENT_SERIALIZERS in Scene/SceneFile.cpp
//   4. a system that acts on it (e.g. stepped from MainRayTracingScene::Update, as the mesh sync is)
namespace Fox {

	namespace Scene {

		class WaterSimulation;
		class NavigationGrid;

		using Transform = Fox::Scene::RayTracing::RayTracingScene::ObjectTransform;

		// What the entity is called, and whether it (and everything under it) is in the scene at all
		struct NameComponent {
			std::string name;
			bool active = true;
		};

		// The scene graph: the entity's parent and its children in order
		struct HierarchyComponent {
			Fox::ECS::Entity parent;
			std::vector<Fox::ECS::Entity> children;
		};

		// Position, rotation and scale relative to the parent (to the world for a root)
		struct TransformComponent {
			Transform local;
		};

		// Where the entity is in the world: the parent's world matrix times its local transform (just the
		// parent's without a TransformComponent). Kept by SceneGraph::UpdateTransforms.
		struct WorldTransformComponent {
			glm::mat4 matrix{ 1.0f };
		};

		// The entity is rendered with this mesh
		struct MeshComponent {
			enum class Type { Model, Sphere, Tree };

			bool enabled = true; // Unchecked: stays in the scene, hidden (no rebuild to toggle)
			Type type = Type::Model;

			// Model: a glTF file
			std::string file;          // .glb / .gltf, from the working directory
			bool displacement = false; // Relief reconstructed from its normal maps (slower to load)

			// Sphere
			float radius = 1.0f;
			SurfaceMaterial material;

			// Tree: generated from its settings (see Editor/TreeEditor)
			std::shared_ptr<Tree> tree;

			// Set by the scene when it is placed in the ray tracing scene
			bool placed = false;
			size_t object = 0;              // Its ray tracing scene object
			glm::mat4 placedMatrix{ 1.0f }; // The world matrix it was last given
			bool shown = true;              // The visibility it was last given

			static const char* TypeName(Type type) {
				switch (type) {
				case Type::Model: return "Model";
				case Type::Sphere: return "Sphere";
				default: return "Tree";
				}
			}
		};

		// The entity is a body of water (Scene/WaterSimulation.h): a rectangle of water on the ground, centred where
		// the entity is, filled up to its height, then left to flow (over the terrain, into hollows) and stirred by
		// wind, rain and splashes. Its rotation and scale are not used.
		struct WaterComponent {
			bool enabled = true;
			glm::vec2 size{ 30.0f, 30.0f }; // Metres along x and z
			float cellSize = 0.2f;          // Metres; at most 512 cells a side

			// Its look: clear water is a dielectric (index of refraction 1.333) that absorbs red first
			glm::vec3 color{ 0.62f, 0.85f, 0.86f }; // What white light is tinted to after `clarity` metres in it
			float clarity = 3.0f;                   // Metres
			float roughness = 0.02f;
			float ripples = 1.0f;     // Strength of the wind's ripples finer than the simulation (shading)

			// Its motion
			float viscosity = 0.001f;  // m^2/s (eddy viscosity: the turbulence the grid cannot resolve; water itself: 1e-6)
			float friction = 0.003f;   // Bed friction
			float windSpeed = 6.0f;    // m/s, 10 m above the water
			float windDirection = 30.0f; // Degrees: where it blows to, from +x towards +z
			float gust = 0.6f;         // 0..1
			float rain = 0.0f;         // Drops per square metre and second
			float dropSize = 0.4f;     // Metres (the ring it makes; at least two cells)
			float dropStrength = 0.02f; // Metres
			bool splashOnClick = true; // Clicking it in edit mode splashes it

			// Foam and spray
			float foam = 1.0f;         // How much foam breaking and churning water makes
			float foamLifetime = 6.0f; // Seconds for foam to fade to a third
			float spray = 1.0f;        // How many droplets are thrown up
			float dropletSize = 0.01f; // Metres (radius)
			uint32_t sprayDroplets = 16384; // Room for droplets in flight (0: no spray)

			// Set by the scene
			bool placed = false;
			size_t surface = 0;
			size_t spraySurface = 0;
			bool hasSpray = false;
			std::shared_ptr<WaterSimulation> simulation;
			glm::mat4 placedMatrix{ 1.0f };

			Fox::Graphics::Vulkan::Material ToMaterial() const {
				Fox::Graphics::Vulkan::Material material;
				material.baseColor[0] = material.baseColor[1] = material.baseColor[2] = 1.0f;
				material.roughness = roughness;
				material.metallic = 0.0f;
				material.transmission = 1.0f;
				material.ior = 1.333f;
				const float tint[3] = { color.r, color.g, color.b };
				material.SetAttenuation(tint, clarity);
				// Water (Shaders/water_waves.slang): the wind (m/s, x and z) and the ripples' strength in fields water
				// has no other use for
				material.layerMaterial = -2;
				const float angle = glm::radians(windDirection);
				material.uvScale[0] = std::cos(angle) * windSpeed;
				material.uvScale[1] = std::sin(angle) * windSpeed;
				material.normalScale = ripples;
				return material;
			}
		};

		// The entity is a rigid body (Scene/Physics.h), simulated while physics plays (Physics > Play, F5): a dynamic
		// body falls and collides and its transform follows, a kinematic one follows its transform and pushes what is
		// in the way, a static one stays. Dimensions are in the entity's space: its world scale scales them.
		struct PhysicsComponent {
			enum class Motion { Static, Dynamic, Kinematic };
			// Auto: a sphere mesh's sphere, a model's convex hull (moving) or its triangles (static), a tree's trunk
			// (a capsule), or else the box
			enum class Shape { Auto, Box, Sphere, Capsule, ConvexHull, Mesh };

			bool enabled = true;
			Motion motion = Motion::Dynamic;
			Shape shape = Shape::Auto;
			glm::vec3 halfExtents{ 0.5f };  // Box
			float radius = 0.5f;            // Sphere, capsule
			float halfHeight = 0.5f;        // Capsule: half its straight part, along y
			glm::vec3 center{ 0.0f };       // The shape's centre, from the entity's origin

			float mass = 0.0f;              // kg; 0: from the density and the shape's volume
			float density = 500.0f;         // kg/m^3 (water 1000: lighter floats)
			float friction = 0.5f;
			float restitution = 0.2f;       // Bounciness
			float linearDamping = 0.05f;
			float angularDamping = 0.05f;
			bool continuous = false;        // Swept collision, for fast small bodies
			int layer = 0;                  // Collision layer (names and which collide: Environment > Collision layers)
			bool trigger = false;           // Collides with nothing, tells its scripts what enters and leaves it

			// In water (a WaterComponent's): lifted by what it displaces, slowed and carried by the flow, and it splashes
			bool floats = true;
			float waterDrag = 0.5f;

			// Set while physics plays
			uint32_t body = 0xffffffffu;

			static const char* MotionName(Motion motion) {
				switch (motion) {
				case Motion::Static: return "Static";
				case Motion::Dynamic: return "Dynamic";
				default: return "Kinematic";
				}
			}
			static const char* ShapeName(Shape shape) {
				switch (shape) {
				case Shape::Auto: return "Auto";
				case Shape::Box: return "Box";
				case Shape::Sphere: return "Sphere";
				case Shape::Capsule: return "Capsule";
				case Shape::ConvexHull: return "Convex hull";
				default: return "Triangle mesh";
				}
			}
		};

		// The entity walks over the ground (Scene/Navigation.h): given a target (Shift + click on the ground in edit
		// mode, or the Inspector), it finds a way there with A* around obstacles (meshes taller than a step), slopes
		// too steep and water too deep, and walks it while the scene plays (F5), turning to face where it goes and
		// keeping its height above the ground. In edit mode the way shows in the view.
		struct NavAgentComponent {
			enum class Forward { PlusZ, MinusZ, PlusX, MinusX };

			bool enabled = true;
			float speed = 2.0f;          // m/s
			float turnRate = 360.0f;     // Degrees per second
			float radius = 0.4f;         // Metres it keeps from obstacles
			float height = 1.8f;         // Metres: what is higher above the ground than this does not block it
			float maxSlope = 35.0f;      // Degrees
			float maxStep = 0.3f;        // Metres it can step up or down anywhere
			float maxWaterDepth = 0.4f;  // Metres it wades through
			float cellSize = 0.5f;       // Metres: the grid it plans on
			Forward forward = Forward::PlusZ; // Which of its axes is its front
			bool alignToGround = false;  // Tilts with the slope

			bool hasTarget = false;
			glm::vec3 target{ 0.0f };

			// Kept by the scene
			std::vector<glm::vec3> path;   // On the ground, from where it was planned
			size_t next = 1;               // The waypoint it walks to
			bool planned = false;          // path is for the current target and start
			bool reachable = false;        // The path ends at the target (not just near it)
			bool arrived = false;
			glm::vec3 plannedFrom{ 0.0f };
			uint64_t plannedGeneration = 0; // The scene's obstacle generation it was planned with
			std::shared_ptr<const NavigationGrid> grid; // The grid it was planned on (shown for the selected agent)
			float replanTimer = 0.0f;
			std::string status;
			// While the scene plays
			float groundOffset = 0.0f;     // Its height above the ground when play started
			float yaw = 0.0f;              // Heading of its front, radians from +x towards +z
			float startYaw = 0.0f;
			glm::quat startRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			glm::vec3 scale{ 1.0f };

			glm::vec3 ForwardAxis() const {
				switch (forward) {
				case Forward::PlusZ: return glm::vec3(0.0f, 0.0f, 1.0f);
				case Forward::MinusZ: return glm::vec3(0.0f, 0.0f, -1.0f);
				case Forward::PlusX: return glm::vec3(1.0f, 0.0f, 0.0f);
				default: return glm::vec3(-1.0f, 0.0f, 0.0f);
				}
			}

			// Plan again (the target, the ground or obstacles changed)
			void Invalidate() {
				planned = false;
				arrived = false;
			}
		};

		// The entity is a volume of smoke: a puff (or box) of `size` metres around it, turned and scaled with it,
		// filled with noisy density drifting with `velocity`. The path tracer scatters light in it
		// (Shaders/volumes.slang): lit by the sun and sky, casting shadows, optionally glowing. Up to 8 at a time.
		struct VolumeComponent {
			bool enabled = true;
			glm::vec3 size{ 4.0f, 4.0f, 4.0f }; // Metres
			bool round = true;                   // A puff (ellipsoid); off: fills the box
			float softness = 0.6f;               // How gradually it thins towards its edge (0..1)
			float density = 0.7f;                // Extinction per metre where densest
			glm::vec3 color{ 0.55f, 0.55f, 0.57f }; // Scattering albedo: white smoke scatters, black absorbs
			float anisotropy = 0.3f;             // -1 back, 0 even, 1 forward scattering
			glm::vec3 glow{ 1.0f, 0.45f, 0.12f };
			float glowStrength = 0.0f;           // Radiance it gives off (fire, embers)
			float noiseSize = 0.8f;              // Metres
			float noiseStrength = 0.8f;          // 0: even density
			glm::vec3 velocity{ 0.0f, 0.6f, 0.0f }; // How its noise drifts (rising smoke), m/s
		};

		// The game looks through the entity while it plays (Play mode, F5): from where it is, along its -Z axis (as
		// glTF cameras and the lights do), with this field of view. Of several, the enabled one with the highest
		// priority; F8 lets go of it (a free camera) and takes it again.
		struct CameraComponent {
			bool enabled = true;
			float fov = 60.0f; // Vertical, degrees
			int priority = 0;
		};

		// The entity plays a sound file (Sounds/: mp3, wav, flac) while the game plays: when play starts (Play on
		// start), or when a script asks (Entity.Sound.Play()). In the world it is heard from where the entity is,
		// following it, quieter with distance; music is background music instead (one track at a time, the same
		// everywhere). The Inspector's Preview plays it in edit mode too.
		struct SoundComponent {
			bool enabled = true;
			std::string file;
			float volume = 1.0f;
			float pitch = 1.0f;
			bool loop = false;
			bool playOnStart = true;
			bool music = false;        // Background music (not placed in the world)
			bool spatial = true;       // Placed at the entity (else heard the same everywhere)
			float minDistance = 2.0f;  // Metres: full volume within this
			float maxDistance = 60.0f; // Metres: quieter until this far

			int64_t voice = 0;         // Playing (Audio::AudioEngine handle; 0: not)
		};

		// The entity scatters copies of a mesh over the ground around it: grass, flowers, stones, bushes. The mesh can be
		// any glTF model or a generated tuft of grass blades. Every copy is its own top level instance of the one mesh
		// (its acceleration structure is shared), so thousands cost little memory. Copies stand on the ground (the
		// terrain or the ground plane) on a jittered grid, with random size and turn, tilted towards the ground's
		// slope; too steep ground is left bare. They can turn to face the camera every frame (billboards: for flat
		// cards) and sway in the wind (the scene's direction). Moving the entity moves them; once it stops they are
		// scattered again over the ground where it is.
		struct ClutterComponent {
			bool enabled = true;
			std::string model;                 // Models/*.glb or .gltf; empty: a generated tuft of grass
			glm::vec2 size{ 20.0f, 20.0f };    // Metres along the entity's x and z, centred on it
			float density = 4.0f;              // Copies per square metre
			uint32_t seed = 1;
			float minScale = 0.7f;
			float maxScale = 1.3f;
			bool randomTurn = true;            // Random turn about the up axis
			float alignToGround = 0.5f;        // 0: upright, 1: along the ground's normal
			float maxSlope = 40.0f;            // Degrees: steeper ground is left bare
			float sink = 0.02f;                // Metres into the ground (times the copy's scale)
			float edgeFade = 0.2f;             // How much of the area (from its edges in) the copies thin out over: no hard border
			int terrainLayer = -1;             // Only where this terrain layer is painted (thinning out where it fades); -1: anywhere
			bool faceCamera = false;           // Billboards: each copy turns about its up axis to face the camera (+Z towards it)
			float sway = 1.0f;                 // In the wind: 0 still, 1 grass-like
			// The generated grass
			glm::vec3 grassColor{ 0.20f, 0.33f, 0.07f };
			float grassHeight = 0.35f;         // Metres
			int blades = 14;                   // Per tuft

			// Set by the scene
			bool placed = false;               // In the last rebuild (whether or not any copy fit)
			bool placedEnabled = false;
			size_t object = SIZE_MAX;          // Its object, SIZE_MAX if none
			glm::mat4 placedMatrix{ 1.0f };    // Its world matrix when scattered
			float movedAgo = -1.0f;            // Seconds since it last moved (scattered again after a moment); < 0: not moved
			uint32_t copies = 0;
			uint64_t triangles = 0;            // Per copy
			std::string problem;
			// Per copy, for turning and swaying them
			std::vector<glm::vec3> positions;
			std::vector<glm::mat4> shapes;     // Tilt to the ground and size (no turn, no position)
			std::vector<float> turns;          // Radians about the up axis
			std::vector<float> phases;
		};

		struct UITextCache; // UI/GameUI.cpp

		// Where a UI element (sprite, screen text) is, in its parent's rectangle: the screen's, or that of the nearest
		// entity above it in the hierarchy that is a UI element (whose position, rotation, scale, alpha and tint it then
		// follows). Pixels are of the reference resolution (UICanvas): the whole UI scales with the window.
		//   anchorMin = anchorMax: a point of the parent's rectangle (0, 0 its top left, 1, 1 its bottom right); the element
		//     is size big, its pivot offset pixels from that point
		//   apart (on an axis): the element stretches between the two points, size added to that (negative: smaller)
		struct UILayout {
			glm::vec2 anchorMin{ 0.5f, 0.5f };
			glm::vec2 anchorMax{ 0.5f, 0.5f };
			glm::vec2 offset{ 0.0f };    // Pixels from the anchor (point), or of the pivot within the stretched rectangle
			glm::vec2 size{ 128.0f, 128.0f }; // Pixels (sprites; a text's size is its text's)
			glm::vec2 pivot{ 0.5f, 0.5f }; // The element's own point there, 0..1 (also the world text's origin)
			float rotation = 0.0f;       // Degrees, clockwise, about the pivot
			glm::vec2 scale{ 1.0f };
			int layer = 0;               // Among its siblings: higher in front
		};

		// Vector text in a TrueType / OpenType font (Fonts/, Text/VectorText.h): the glyphs' outlines, sharp at any size.
		//   Screen + Flat      the game's UI: drawn over the image at a fraction of the screen (GameUI)
		//   World  + Flat      a flat sign of real geometry in the world, lit like everything else; or a billboard,
		//                      turning to face the camera
		//   World  + Extruded  3D letters (depth), subdivided and displaced by a height map if wanted
		//   Screen + Extruded  3D letters in the UI: geometry held in front of the camera, lit by the world around it;
		//                      only rays from the camera see it (no shadows on the world, no reflections)
		// A fill image textures it and a mask (alpha, or brightness) cuts it: decorated text. Sizes: in the world metres
		// of an em, on the screen pixels of an em at 1080 lines.
		struct TextComponent {
			enum class Space { World, Screen };
			enum class Style { Flat, Extruded };
			bool enabled = true;
			std::string text = "Text";
			std::string font;            // Fonts/*.ttf or .otf; empty: the first one there
			Space space = Space::World;
			Style style = Style::Flat;
			bool billboard = false;      // World: turns to face the camera
			float size = 1.0f;           // World: metres; screen: pixels at 1080 lines
			int align = 1;               // 0 left, 1 centre, 2 right
			float lineSpacing = 1.0f;    // Of the font's line height
			float letterSpacing = 0.0f;  // Ems
			glm::vec4 color{ 1.0f };     // On the screen its alpha too
			std::string fill;            // An image textured over its bounds
			std::string mask;            // An image whose alpha (or brightness) cuts it out over its bounds
			float roughness = 0.4f;      // Geometry (world, 3D)
			float metallic = 0.0f;
			float emission = 0.0f;       // Glow: emitted radiance of its colour
			float depth = 0.2f;          // Extruded: ems
			int subdivisions = 0;        // Extruded: every triangle split in four this many times (for displacement)
			std::string displacement;    // Extruded: a height map over its bounds (0.5 grey: none)
			float displacementScale = 0.03f; // Ems from black to white
			UILayout layout{ { 0.5f, 0.1f }, { 0.5f, 0.1f } }; // Screen: where (UILayout); its pivot is the world text's origin too

			// Set by the scene
			bool placed = false;
			std::string placedKey;
			size_t object = SIZE_MAX;
			uint64_t triangles = 0;
			std::string problem;
			std::shared_ptr<UITextCache> cache; // Screen text's outlines
			std::string uiTextureKey;
			int uiTexture = -1;
		};

		// An image on the screen (the game's UI), over the 3D image and under the editor
		struct SpriteComponent {
			bool enabled = true;
			std::string image;           // Empty: a plain rectangle of the colour
			std::string mask;            // Alpha (or brightness) cutting it out
			glm::vec4 color{ 1.0f };     // Multiplies the image
			UILayout layout;
			bool keepAspect = true;      // Width from the height and the image's proportions (not when stretched across)

			// Set by the UI
			std::string textureKey;
			int texture = -1;
			std::string problem;
		};

		// Animates its entity's UI element (sprite or screen text): steps played in order, each moving one property from
		// where the steps before left it (or from its own start) to a value, over a time, with an easing. A step "with the
		// previous" starts with the step before it instead of after. Values are on top of the layout: position an offset
		// in pixels, scale a factor, rotation degrees added, alpha and tint factors. Children follow (alpha and tint too).
		struct UIAnimationComponent {
			enum class Property { Position, Scale, Rotation, Alpha, Tint };
			enum class Easing {
				Linear, SineIn, SineOut, SineInOut, QuadIn, QuadOut, QuadInOut, CubicIn, CubicOut, CubicInOut,
				ExpoIn, ExpoOut, ExpoInOut, BackIn, BackOut, BackInOut, ElasticIn, ElasticOut, ElasticInOut, BounceIn, BounceOut, BounceInOut
			};
			enum class Repeat { Once, Loop, PingPong };
			struct Step {
				Property property = Property::Position;
				glm::vec4 to{ 0.0f };     // Position: x, y; scale: x, y; rotation, alpha: x; tint: r, g, b
				bool useFrom = false;     // From this value, not from where the steps before left it
				glm::vec4 from{ 0.0f };
				float duration = 0.5f;    // Seconds
				float delay = 0.0f;       // Before it starts
				Easing easing = Easing::QuadOut;
				bool withPrevious = false; // Starts with the step before (in parallel), not after it
			};
			bool enabled = true;
			std::vector<Step> steps;
			Repeat repeat = Repeat::Once;
			float speed = 1.0f;
			bool playOnStart = true;      // When the game starts playing (UI.Play starts it from a script)
			bool previewInEditor = true;  // Plays (on repeat) in edit mode

			// Set by the UI
			float time = 0.0f;
			bool playing = false;
			bool triggered = false;      // Played by a button or script: to its end, also in edit mode
		};

		// A UI in the 3D world: the UI elements (sprites, screen texts, with their layouts, animations and buttons) under
		// this entity in the hierarchy are drawn into its own picture instead of onto the screen, and that picture is a
		// panel in the scene: lit like everything else (and glowing, if wanted), hidden behind what is in front of it,
		// seen in reflections. It keeps the entity's orientation (its front is +Z), or turns to face the camera
		// (billboard). Its buttons are clicked where the mouse ray meets it; within interactDistance of the camera they
		// free the pointer outside edit mode.
		struct UICanvasComponent {
			bool enabled = true;
			glm::vec2 resolution{ 1024.0f, 512.0f }; // Its pixels: its UI's rectangle (and the picture's size)
			float width = 2.0f;           // Metres (the height from the resolution's proportions)
			bool billboard = false;       // Turns to face the camera
			float glow = 1.0f;            // Emitted light of the picture (0: lit only, like paint)
			float interactDistance = 12.0f; // Metres: closer, its buttons take the pointer

			// Set by the scene and the UI
			bool placed = false;
			std::string placedKey;
			size_t object = SIZE_MAX;
			int texture = -1;
			uint64_t drawnHash = 0;
			std::vector<uint8_t> pixels;
		};

		// The entity's UI element (sprite or screen text) is a button: under the mouse it takes the hover tint and scale,
		// pressed the pressed scale. A click (in play or edit mode) plays the UI animations of the entities named in
		// playOnClick (comma separated) from the start, and those of everything under them if withChildren. Scripts can
		// ask UI.Clicked(entity).
		struct UIButtonComponent {
			bool enabled = true;
			std::string playOnClick;      // Entity names, e.g. "Inventory panel"
			bool withChildren = true;     // Their children's animations too
			glm::vec3 hoverTint{ 1.0f, 0.85f, 0.6f };
			float hoverScale = 1.06f;
			float pressedScale = 0.94f;

			// Set by the UI
			bool hovered = false;
			bool pressed = false;
			bool clicked = false;         // In the frame it was clicked (until the next UI frame)
			uint32_t clicks = 0;
		};

		// One particle of a ParticleComponent (simulated on the CPU, MainRayTracingScene::UpdateParticles)
		struct Particle {
			glm::vec3 position{ 0.0f }, previousPosition{ 0.0f };
			glm::vec3 velocity{ 0.0f };
			glm::quat orientation{ 1.0f, 0.0f, 0.0f, 0.0f }, previousOrientation{ 1.0f, 0.0f, 0.0f, 0.0f }; // Leaves
			glm::vec3 spin{ 0.0f };      // Leaves: angular velocity (rad/s, world); sprites: x = turn in the image plane
			float size = 0.1f;
			float previousSize = 0.1f;
			float age = 0.0f;
			float life = 1.0f;
			float phase = 0.0f;          // Decorrelates flicker and swirl
			bool landed = false;         // Leaves: lying on the ground or the water
			bool mote = false;           // Orbs: a glittering mote around them, not an orb
			float shade = 0.0f;          // Orbs: 0 the first colour .. 1 the second
			float hover = 1.0f;          // Orbs: metres above the ground it floats at
			bool alive = false;
		};

		// The entity emits particles, simulated on the CPU and traced as real geometry (a quad each):
		//   Fire    glowing flame sprites rising, cooling from white-yellow to red and soot; a flickering point light
		//   Magic   glowing sparks swirling up around it in the colour; a point light in the colour
		//   Orbs    glowing orbs drifting and hovering over the area, each its own shade between two colours, with
		//           glittering motes twinkling around them; the first dozen orbs light what is under them (enchanted)
		//   Leaves  leaves (a texture with alpha: its normal, roughness, metallic and ambient occlusion maps are found
		//           beside it) flying in the scene's wind and fluttering down. Each is a rigid plate: the air pushes it
		//           along its normal in proportion to its area facing the relative wind, at the centre of pressure
		//           (off centre, upwind, unless the flow is head-on): that turns it, damped by the air. They land and
		//           lie on the ground (or float on water) until their life is over.
		// Fire, magic and orbs face the camera and cast no shadows; leaves cast them.
		struct ParticleComponent {
			enum class Effect { Fire, Magic, Leaves, Orbs };
			bool enabled = true;
			Effect effect = Effect::Fire;
			uint32_t maxParticles = 320;
			float rate = 260.0f;         // Born per second
			float lifetime = 0.75f;      // Seconds (leaves: flying and lying)
			float size = 0.5f;           // Metres
			float speed = 1.0f;          // Fire: rising (m/s); magic: around (m/s); leaves: thrown (m/s)
			float spread = 0.35f;        // Metres: fire's base radius; magic's ring; leaves: half the area they start over
			float height = 0.0f;         // Leaves: metres above the entity they start at
			glm::vec3 color{ 1.0f, 0.55f, 0.2f }; // Fire: its light's colour; magic: the sparks' and light's colour
			glm::vec3 color2{ 0.85f, 0.45f, 1.0f }; // Orbs: the other colour (each orb is a shade between the two)
			float brightness = 12.0f;    // Glow (emitted radiance)
			float windSpeed = 4.0f;      // m/s of the scene's wind (Environment > Wind towards) it is carried by
			bool light = true;           // Fire and magic: a point light that flickers with it; orbs: one each (the first dozen)
			float lightIntensity = 4.0f; // Irradiance at 1 m
			std::string texture;         // Leaves: the albedo image (empty: Textures/vahtera/vahtera_leaf.png)

			// The defaults of an effect
			static ParticleComponent Preset(Effect effect) {
				ParticleComponent p;
				p.effect = effect;
				if (effect == Effect::Magic) {
					p.maxParticles = 256; p.rate = 120.0f; p.lifetime = 1.6f; p.size = 0.09f; p.speed = 2.5f; p.spread = 0.6f;
					p.color = glm::vec3(0.35f, 0.6f, 1.0f); p.brightness = 25.0f; p.windSpeed = 0.0f; p.lightIntensity = 2.0f;
				}
				else if (effect == Effect::Orbs) {
					p.maxParticles = 160; p.rate = 8.0f; p.lifetime = 12.0f; p.size = 0.18f; p.speed = 0.35f; p.spread = 10.0f;
					p.height = 1.2f; p.color = glm::vec3(0.55f, 0.85f, 1.0f); p.color2 = glm::vec3(0.85f, 0.45f, 1.0f);
					p.brightness = 4.0f; p.windSpeed = 0.6f; p.lightIntensity = 1.2f; // Not too bright: the tone curve bleaches very bright colours white
				}
				else if (effect == Effect::Leaves) {
					p.maxParticles = 300; p.rate = 10.0f; p.lifetime = 25.0f; p.size = 0.14f; p.speed = 0.5f; p.spread = 6.0f;
					p.height = 8.0f; p.color = glm::vec3(1.0f); p.brightness = 0.0f; p.windSpeed = 5.0f; p.light = false;
				}
				return p;
			}

			// Set by the scene
			bool placed = false;
			Effect placedEffect = Effect::Fire;
			uint32_t placedMax = 0;
			std::string placedTexture;
			size_t surface = SIZE_MAX;   // Its simulated surface
			std::vector<size_t> lights;  // Fire and magic: one; orbs: one per orb slot of the first dozen
			std::vector<Particle> particles;
			float spawnCarry = 0.0f;
			uint32_t random = 1;
			std::string problem;
		};

		// The entity is an instance of a prefab: an entity tree saved in a file (Prefabs/*.prefab.json), placed any
		// number of times. Its components and everything under it come from the prefab, with this instance's own
		// changes on top (overrides): changing the prefab changes every instance, except where an instance has
		// changed something itself. Its transform and name are its own.
		//
		// Overrides are found by comparing the instance with its prefab (MainRayTracingScene::GetPrefabOverrides);
		// they are kept here (when the scene is saved) as JSON text by key: "path|component|field", where path names
		// the entity under the instance ("" for itself, "Wheel/Hub" below it), "@active" in place of a component is
		// whether it is active, and a key without a field is a whole component added (or removed: null).
		struct PrefabComponent {
			std::string file;
			std::map<std::string, std::string> overrides;
		};

		// C# scripts on the entity (classes deriving from Fox.Script in Scripts/*.cs): while the game plays each gets
		// OnStart, then OnUpdate every frame. Their public fields' values are set here (as text) and saved with the
		// scene; a field not listed keeps the value the class gives it.
		struct ScriptComponent {
			struct Entry {
				std::string className;
				std::map<std::string, std::string> fields;
				int64_t instance = 0; // Its running script while playing (0: none)
			};
			bool enabled = true;
			std::vector<Entry> scripts;
		};

		// The entity gives light. It shines along its -Z axis (as glTF lights do): a directional light's entity
		// is turned to aim it (its position does not matter), point and spot lights are where the entity is.
		struct LightComponent {
			enum class Type { Directional, Point, Spot };

			bool enabled = true; // Unchecked: dark, without a rebuild
			Type type = Type::Directional;
			glm::vec3 color{ 1.0f };
			// Directional: irradiance on a surface facing it (times colour). Point / spot: intensity, the irradiance
			// it gives 1 m away (falls off with the square of the distance).
			float intensity = 10.0f;
			float angularRadius = 0.27f;   // Directional: apparent radius of its disc in degrees (the real sun 0.27). Larger = softer shadows
			float radius = 0.0f;           // Point / spot: radius of the bulb in metres. 0 = hard shadows
			float innerConeAngle = 20.0f;  // Spot: full light within this angle of its axis (degrees)
			float outerConeAngle = 30.0f;  // Spot: none beyond this one

			// Set by the scene when it is placed in the ray tracing scene
			bool placed = false;
			size_t index = 0;                      // In the scene's light buffer
			Fox::Graphics::Vulkan::Light uploaded; // As it was last given to the scene

			// The shader's light for an entity with this world matrix
			Fox::Graphics::Vulkan::Light ToLight(const glm::mat4& world, bool shown) const {
				Fox::Graphics::Vulkan::Light light{};
				const glm::vec3 back = glm::vec3(world[2]); // +Z: where it shines from
				const glm::vec3 towardsLight = glm::dot(back, back) > 1e-12f ? glm::normalize(back) : glm::vec3(0.0f, 1.0f, 0.0f);
				light.color = shown ? color * intensity : glm::vec3(0.0f);
				switch (type) {
				case Type::Directional:
					light.type = 0;
					light.normal = towardsLight;
					light.area = glm::radians(std::max(angularRadius, 0.001f));
					break;
				case Type::Point:
				case Type::Spot: {
					light.type = type == Type::Point ? 1 : 2;
					light.position = glm::vec3(world[3]);
					light.normal = -towardsLight;
					light.area = std::max(radius, 0.0f);
					const float outer = glm::radians(glm::clamp(outerConeAngle, 0.1f, 179.0f));
					const float inner = glm::radians(glm::clamp(std::min(innerConeAngle, outerConeAngle - 0.05f), 0.0f, 179.0f));
					light.tangentX = glm::vec3(std::cos(outer), std::cos(inner), 0.0f);
					break;
				}
				}
				return light;
			}

			static bool Same(const Fox::Graphics::Vulkan::Light& a, const Fox::Graphics::Vulkan::Light& b) {
				return a.type == b.type && a.position == b.position && a.normal == b.normal && a.tangentX == b.tangentX
					&& a.color == b.color && a.area == b.area;
			}

			static const char* TypeName(Type type) {
				switch (type) {
				case Type::Directional: return "Directional light";
				case Type::Point: return "Point light";
				default: return "Spot light";
				}
			}
		};
	}
}
