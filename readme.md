# 🦊 Fox Vulkan Path Tracing Renderer

A low-level forward path tracing renderer built from scratch using the **Vulkan API** and **C++**

![Fox Vulkan Path Tracing Renderer Screenshot](FoxVulkanPathTracer/Textures/box.png)

## 📌 Overview

The **Fox Vulkan Path Tracing Renderer** leverages modern GPU hardware to produce photorealistic imagery. It features robust support for the glTF 2.0 standard and utilizes a physically based rendering (PBR) pipeline to accurately simulate light interaction.

* **API:** Vulkan (Modern C++ binding)
* **Shading Model:**  Physically Based Rendering (PBR) using the Cook-Torrance microfacet BRDF.
* **Format:** Industry-standard glTF 2.0 (both `.gltf` and `.glb` files).

---

## 🚀 Key Features

* **Pipeline:** Engineered from the ground up to utilize hardware-accelerated ray tracing and modern memory management.
* **glTF 2.0 Asset Loading:** Built-in parser supporting both binary (`.glb`) and text-based (`.gltf`) scenes.
* **Path Tracing** Iterative, unbiased path tracer with multiple importance sampling, Russian roulette and soft shadows.
* **Physically Based Shading** Layered glTF metallic-roughness BSDF: GGX microfacet specular (visible-normal importance sampling,
  height-correlated Smith, multiple-scattering energy compensation) over an energy-conserving diffuse layer, plus thin glass.
  Supports base colour, metallic-roughness, normal and emissive textures and `KHR_texture_transform`,
  `KHR_materials_transmission`, `KHR_materials_specular`, `KHR_materials_emissive_strength`, `KHR_materials_ior`,
  `KHR_materials_volume`.
* **Refraction** Solid glass bends light where it enters and leaves, with exact dielectric Fresnel reflection, total
  internal reflection, rough (frosted) glass through GGX microfacets and coloured absorption inside (Beer–Lambert).
  The default scene (`Scenes/default.json`) has a glass ball in the foreground.
* **Lighting** Importance-sampled HDR environment map, emissive quads as area lights, and light components: a sun with soft shadows, point and spot lights.
* **Emissive surfaces** Every surface that glows lights its surroundings:
  * glTF models: `emissiveFactor` × `KHR_materials_emissive_strength` × the emissive map;
  * spheres: Inspector > **Emission**, **Emission strength** and **Emissive map**, or the **Lamp (glowing)** preset; saved with the scene.

  Each emissive triangle is also a light (at most 65536 per mesh). Light sampling picks one with a chance in proportion to the power it gives off, and the light at the sampled point is read from the emissive map. Hits on glowing surfaces are weighed against light sampling (multiple importance sampling), so small, bright emitters come out clean rather than as fireflies. When there are both, half of the light samples go to the sun and lamps, half to emissive surfaces.
* **Displacement Mapping** Surfaces are tessellated and displaced when a model loads, so the relief is real geometry
  (silhouettes, shadows, reflections). Height maps can be assigned per material; models without them (glTF has no slot
  for displacement) get heights reconstructed from their normal maps.
* **Denoising** Pluggable denoisers, switchable at runtime:
  * **DLSS Ray Reconstruction** (NVIDIA, optional): neural denoising + anti-aliasing with the newest transformer model.
  * **SVGF** (Spatiotemporal Variance-Guided Filtering): temporal reprojection + variance-guided a-trous wavelet filter, in compute shaders.
  * **Accumulation**: plain averaging while the camera is still, as the ground-truth reference.
* **HDR Pipeline** Floating point rendering and an AgX tone mapping pass with exposure control.
* **Image grade** The Environment window's **Image** section sets exposure (stops, before the tone curve), then brightness,
  contrast (about mid grey) and saturation (against the luma) on the tone mapped image (`Shaders/tonemapping_frag.slang`).
  All four are saved with the scene (`"environment": { "image": { ... } }`), and **Reset image** brings back the neutral grade.
* **GPU Profiler** Window > **GPU Profiler** (or F1 "gpu profiler") shows:
  * the GPU's busy time and the CPU frame time as graphs of the last 240 frames;
  * each pass's averaged GPU time from timestamp queries (skin, LOD select, clusters, instances, TLAS, trace, denoise, tonemap);
  * what the acceleration structures hold:
    * **Triangles traced**: the triangles in the visible TLAS instances, counting each instance, plus the tree clusters selected this frame (estimated);
    * **Triangles stored**: each BLAS once;
    * the BLAS count and memory, TLAS instances, cluster memory, vertices, materials and lights;
  * paths per second;
  * this process's video memory use against the driver's budget (`VK_EXT_memory_budget`, when the driver has it).
* **Skinning & Wind** Skinned meshes are deformed on the GPU every frame (linear blend skinning in a compute shader,
  acceleration structures refitted). A procedural tree is built out of cylinders with a bone per cylinder, and every
  joint is an angular spring pushed by gusty wind. One wind direction for the whole scene: every tree sways the same way.
* **Game UI and text** Vector text in any TrueType / OpenType font from `Fonts/`. The Content Browser lists that folder, and double-clicking a font adds a UI text in it.
  * **How text is drawn:** glyph outlines (their quadratic and cubic curves) are flattened as finely as the size needs, laid out with the font's advances and kerning, and filled by triangulating them, holes and all. Nothing is a bitmap, so edges stay sharp at any size. A missing character leaves a space-wide gap. Code: `Text/VectorText`, triangulation after mapbox/earcut.
  * **Text component** (**Create > UI** or **Create > Text**, or added in the Inspector), by where it is and its style:
    * **Screen, flat:** the game's UI, drawn over the image and under the editor. It sits at a screen position with a pivot and a layer, at a size in pixels at 1080 lines, so it scales with the window. Edges get a one-pixel anti-aliased fringe.
    * **World, flat:** a sign of real geometry, lit, shadowed and reflected like everything else. **Billboard** turns it to face the camera every frame.
    * **World, 3D:** extruded letters (Depth). **Subdivisions** split every triangle in four, up to 5 times. A **Displacement** height map over the text's bounds then moves the surface along its smoothed normals. Positions are welded, so no cracks open between faces and walls.
    * **Screen, 3D:** extruded letters held 25 cm in front of the camera, placed and sized like UI text and lit by the world around the camera. Only camera rays see them (instance mask `INSTANCE_MASK_UI`), so they cast no shadows and show in no reflections.
  * **Decorated text:** a **Fill image** textures the text over its bounds, and a **Mask** cuts it out where the mask is transparent, or dark if it has no alpha. Both work in the UI and in the world, where they combine into one texture with the alpha tested. In the world, text can also have roughness, metallic and **Glow** (emission, so neon signs light their surroundings).
  * **Sprite component** (Create > UI > Sprite, or **Panel**: a dark see-through box to hold others): an image on the screen, with a mask, a colour, and its image's proportions kept if wanted.
  * **UI layout:** every UI element (sprite or screen text) has a layout in its parent's rectangle. The parent is the screen, or the nearest UI element above it in the Hierarchy.
    * **Anchors:** a point (a corner, an edge, the middle), or two points apart on an axis so that it stretches across its parent. The Inspector has presets.
    * **Offset and size:** in pixels from the anchor; when stretched, the size is added to the stretch.
    * **Pivot, rotation, scale and layer:** the layer orders siblings. Parents are drawn first, and children follow their transform, alpha and tint.
    * **Scaling:** pixels are of a reference resolution, 1920 × 1080 by default (Environment > UI canvas). The whole UI scales with the window by its width, its height, or between the two (**Match**). Anchored elements keep their corners, and stretched ones keep spanning the screen.
    * **Create:** Create > UI puts a new element under the selected one when that is a UI element.
  * **UI animation component:** steps played in sequence, each starting after the one before or **with the previous** one.
    * **Properties:** position (an offset in pixels), scale, rotation, alpha and tint, each step taking one property from where the steps before left it (or from its own **From**) to a value.
    * **Timing:** each step has a duration, a delay and an easing. There are 22 easings: linear, and in, out and in-out versions of sine, quad, cubic, expo, back, elastic and bounce.
    * **Playback:** Once, Loop or Ping-pong, at a speed. Animations play when the game starts and preview in edit mode. Scripts call `UI.Play(entity)` and `UI.Stop(entity)`.
    * **Examples:** pop in, slide in, bounce down, pulse, spin and wobble, in the Inspector's example list.
  * **UI button component:** makes a sprite or screen text clickable, in play mode and in edit mode.
    * **Feedback:** a hover tint and scale, and a pressed scale.
    * **Action:** a click plays the UI animations of the entities named in **Play on click** from the start, with their children's if wanted. A clicked button doesn't also select what is behind it in the editor.
    * **Switch on click:** a click also makes the entities named in **Switch on click** active, for **For (s)** seconds, and then inactive again; a click meanwhile starts the time over, and 0 seconds leaves them on. This works in edit mode and play mode alike, without a script.
    * **Pointer:** outside edit mode the mouse normally looks around. While any UI button is on the screen the pointer is freed to click, and the right mouse button looks around. When the buttons are gone, the mouse looks around again.
    * **Scripts:** `UI.Clicked(entity)` tells whether the button was clicked this frame.
  * **World UI canvas** (Create > UI > World Canvas, or World Canvas (billboard)): a UI in the 3D world.
    * **What it draws:** the UI elements under the canvas entity (sprites, texts, layouts, animations and buttons, exactly as on the screen) are laid out in the canvas's own pixels (Resolution). They are drawn on the CPU into its picture, again only when something on it changed.
    * **How it shows:** the picture is a panel of real geometry in the scene. It is lit, hidden behind what is in front of it, and seen in reflections, and its see-through parts are holes (alpha tested). **Glow** makes it give off light like a screen, and lights what is around it.
    * **Orientation:** it keeps its entity's orientation (front +Z), or turns to face the camera (**Billboard**).
    * **Buttons:** clicked where the mouse ray meets the panel. Within **Interact distance** of the camera they free the pointer outside edit mode.
  * **Cabin UI in my_yard:** both are children of the Cabin, so they move with it.
    * **Cabin sign:** a fixed bark-framed board in front of the house with "THE OLD CABIN", "BUILT IN 1887", a rocking leaf and a **REST HERE** button. The button pops up "YOU FEEL RESTED", which fades away again.
    * **Cabin marker:** a glowing billboard label, "CABIN" with a pulsing orb, bobbing above the roof.
  * **Sample in my_yard:** the "Game UI" entity holds an inventory panel with five item slots (icon and count) and a **DISCARD ALL** button. A click makes the items pop, spin and shrink away one after another, then the panel slides down and fades out. Starting or stopping play brings it back.
  * **Scripts:** `UI.SetText(entity, "Score: 10")` changes a text. Text on the screen changes at once; text in the world is rebuilt as geometry with the next scene rebuild.
* **Particle effects** **Create > Effects > Fire / Magic / Falling Leaves**, or a **Particles** component on any entity.
  * **Simulation:** particles are simulated on the CPU. Each is a quad of real geometry in a simulated surface, with its structure rebuilt every frame and previous positions uploaded, so motion vectors and DLSS see them move.
  * **Fire:** flame sprites (`Textures/particles/flame.png`) rise with buoyancy and turbulence and are carried by the wind. Each glows by its own heat, cooling from white-yellow through orange and red to soot (`FireColor` in `Shaders/common.slang`). A flickering point light lights what is around.
  * **Magic:** sparks (`Textures/particles/spark.png`) swirl up around the entity in its colour and twinkle, with a point light in the colour.
  * **Magic orbs** (Create > Effects > Magic Orbs): glowing orbs (`Textures/particles/orb.png`) wander slowly over an area on currents of their own, with a breath of the scene's wind.
    * **Hover:** they float and bob over the ground, at half to one and a half times the hover height.
    * **Colour:** each orb is its own shade between two colours.
    * **Motes:** three in four particles are tiny motes that twinkle around them.
    * **Lights:** the first dozen orbs each carry a point light in their shade, so the ground and trees under them glow.
    * **Fading:** orbs fade in and out over 1.5 s and pulse gently.
    * **Brightness:** keep it low, about 2–6. Very bright colours are bleached toward white by the tone curve.
  * **Sparks** (Create > Effects > Sparks): bursts of glowing sparks.
    * **Bursts:** **Burst** particles at once, at random times, **Rate** times a second on average.
    * **Direction:** they fly out away from the entity (its +Z points away from a wall it is on), white hot fading to their colour.
    * **Motion:** they fall, are slowed by the air, and bounce off the ground.
  * **Rain** (Create > Effects > Rain): raindrops over an area around the camera, which the rain follows.
    * **Drops:** carried by the scene's wind, each drawn as a streak along its fall, as long as its fall in a 1/30 s exposure and turned to face the camera.
    * **Splashes:** drops splash where they land, on the ground or on the water.
    * **Wet scene:** while it rains, the scene gets wet (`camera.wetness`, `Shaders/closest_hit.slang`). Surfaces darken and turn glossy, tops most, walls some, undersides little, and the lake ripples with the drops. Afterwards it dries over tens of seconds.
    * **Ramp:** rain eases in and out when its entity is switched on and off. Particles in flight live on when an emitter is switched off, for any effect.
    * **Not handled:** rain is not stopped by roofs, so it falls through them, and covered surfaces get wet too.
  * **In my_yard:** a "Rain" entity, inactive. The cabin sign's REST HERE button switches it on for 10 seconds (**Switch on click**), in edit mode and play mode alike; another click while it rains starts the 10 seconds again.
  * **From a script:** `Scripts/RainOnClick.cs` shows the same done in C# (`UI.Clicked`, `Entity.Active`); scripts run in play mode only.
  * **Light emitting particles** (any effect; orbs have their own lights): each frame the **Lit particles** brightest particles each carry a point light in their colour and current brightness, at **Particle light** irradiance at 1 m. Sparks thus flash on the walls around them as each burst flares and dies.
  * **In my_yard:** "Wall sparks", parented to the Cabin by its front wall, bursts blue-white sparks whose lights flash on the logs, the door and the porch. It shows best at dusk; the 9 key dims the sky.
* **Cloth** (Create > Cloth, or the Cloth component): sheets of fabric simulated on the CPU (`Scene/Cloth.h`), in edit mode and play mode alike.
  * **Presets:** a textured **Curtain** (pinned along its top edge), a plain **Flag** (pinned along one side, fluttering in the wind), a plain **Sheet** (pinned nowhere: it falls and drapes over what is under it) and a textured, tearable **Net** (held at its four corners, lying flat).
  * **Look:** plain (its colour), or textured: an albedo image, with the normal, roughness, metallic and occlusion maps beside it found by their names (as `fabric_normal.png` beside `fabric_albedo.png`). **Tiling** repeats the image; **Alpha cutoff** makes its transparent parts holes (lace, mesh). `Textures/fabric/` holds a woven tartan set (albedo, normal, roughness). Colour, roughness and tiling change live; size, resolution and texture rebuild the scene once let go of.
  * **Shape:** **Size** in metres, **Resolution** segments across. It hangs down from its entity (the top edge's middle at the origin, in the entity's x-y plane), or lies **flat** (x-z, centred). **Pinned:** none, top edge, top corners, left edge or four corners; the pins follow the entity, so moving it drags the cloth.
  * **Simulation:** XPBD in fixed small steps (**Substeps** per 1/60 s). Stretch and shear come from the triangles' edges, bending from springs across each pair of triangles (**Softness**). **Weight** is in kg/m². Long range attachments keep a hanging cloth from sagging and stretching, however fine it is.
  * **Air and wind:** the air pushes on each particle by the way the cloth faces there (**Air drag**), so cloth falls softly and flutters, and the scene's wind (Environment > Wind towards, with its gusts) blows on it at **Wind** m/s.
  * **Collisions:** with the ground (terrain or ground plane), with **itself** (folds keep apart), and with physics shapes that are spheres, capsules or boxes (a sphere mesh's own sphere, a tree's trunk). A model's hull or triangle mesh is not used: give it a box. **Friction** holds it where it lies, on a slope gentler than its friction. In play mode the cloth pushes back on dynamic bodies, so a net slows the ball it catches.
  * **Tearing** (**Tearable**): an edge stretched past **Tears at** times its length splits the cloth there, across the pull, as woven cloth tears, so a tear runs on as the pull goes on. A heavy hit, a weight or a hard pull tears it; a piece torn free falls. A cloth settles for a second after it is made before it can tear.
  * **Mouse** (edit mode and play mode): **Alt + drag** takes hold of the cloth and pulls it (pull a tearable one hard and it tears); **Alt + Shift + drag** cuts it like a knife, along the way the mouse goes. **Reset** in the Inspector, and stopping play, make it whole again.
  * **Rendering:** each triangle has its own three vertices, so tearing changes only which particle each reads, and the buffers keep their size. Its acceleration structure is rebuilt every frame. Cloth is two sided, and casts shadows.
  * **Cost:** each cloth steps on a thread of its own; the Inspector shows its time. A 40 × 40 net with self collision takes about 6 ms a frame in Release (the Debug build is several times slower).
  * **Test scene:** `Scenes/cloth.json` (File > Open Scene..., or the content browser) has the curtain, the flag, a sheet that drapes a ball, and the net. Play (F5) drops a heavy ball into the net, which tears.
  * **Fire, magic and orbs** face the camera and cast no shadows (instance mask `INSTANCE_MASK_EFFECT`). Every ray still sees them, and bounce rays carry their glow.
  * **Falling leaves:** each leaf is a rigid plate in the scene's wind (Environment > Wind towards, at the emitter's wind speed, gusting).
    * **Force:** the air pushes the leaf along its normal with ½ ρ C<sub>N</sub> A<sub>projected</sub> \|u\|², where A<sub>projected</sub> = A \|n · û\| is its area facing the relative wind u. A little skin friction acts along it. Velocity follows F / m, with an areal density of 0.15 kg/m².
    * **Torque:** that push acts at the centre of pressure, upwind of the middle by up to a quarter of the leaf the more the flow runs along it, and at the middle when it meets the leaf head on. So the torque is in proportion to the projected area too. It turns the leaf with a plate's moments of inertia (gyroscopic term included), damped by the air, which gives fluttering and tumbling.
    * **Landing:** leaves land and lie on the ground, or float on water, until their life is over.
    * **Textures:** the albedo is `Textures/vahtera/vahtera_leaf.png` by default (its alpha cuts the leaf out). Normal, roughness, metallic and ambient occlusion maps in the same folder are used when their file names say so, for example `vahtera_leaf_normal.png` or `vahtera_leaf_roughness.png`.
  * **Settings:** max particles, rate, lifetime, size, speed, spread, start height, colour, brightness, wind and light.
* **Clutter (instanced grass, flowers, stones)** **Create > Clutter (grass)**, or add a **Clutter** component to any entity.
  * **What is scattered:** copies of any glTF model, or a generated tuft of grass blades (colour, height, blades per tuft).
  * **Where:** over the ground around the entity, on a jittered grid: area, density per m², seed, scale range, random turn, alignment to the ground's slope, a maximum slope, and sink.
  * **Where not:** never under water, meaning ground below a Water component's surface inside its rectangle.
  * **On terrain layer:** copies grow only where that layer is painted, thinning out where it fades. Grass follows the painted grass and leaves paths bare.
  * **Edge fade:** copies thin out toward the area's border, so patches have no hard edges and overlapping patches blend.
  * **In my_yard:** a valley-wide patch, 240 × 240 m at 1.6 per m², and a denser meadow near the default camera, 80 × 70 m at 6 per m². Both sit on the Grass layer, under the "Grass" entity. On an RTX 3070 Laptop on AC power, the frame took about 12.5 ms of GPU time with the grass.
  * **How it's instanced:** every copy is its own top-level instance of the one mesh, sharing its acceleration structure. 10,000 tufts of grass cost one tuft's memory. The grass blades are real geometry, not alpha-tested cards, so rays pass between them at full speed.
  * **Face the camera:** billboards. Each copy turns about its up axis every frame so its +Z faces the camera, for flat cards. In a path tracer, shadows and reflections also see them turned toward the camera.
  * **Sway:** copies within 80 m lean with the gusts of the scene's wind and flutter.
  * **Moving the entity:** the copies follow it, and half a second after it stops they are scattered again over the ground there. **Scatter again** does it by hand, for example after sculpting the terrain.
  * **Budget:** up to 98,304 top-level instances for all clutter; the top-level structure now holds 131,072.
  * **Saved:** with the scene, as `"clutter"` in the entity's components.
  * **Measured** in my_yard (laptop on battery): a 40 × 40 m grass patch at 6 tufts per m² (9,600 copies, about 1 M triangles traced) made the top-level build go from 0.2 to 0.7 ms. The trace time did not measurably change.
* **Leaves & Opacity Micromaps** The tree carries alpha tested leaf cards. Opacity micromaps (`VK_EXT_opacity_micromap`)
  resolve most of each leaf's cutout in hardware; an any-hit shader alpha tests the rest.
* **Tree Editor** Add, duplicate and remove trees while the scene renders, and change everything about each one: shape,
  randomness, a spline cross-section for the trunk, a spline curve for its centre line, leaves, wind, and each tree's
  own textures. Built with Dear ImGui.
* **Scene Editing** Click an object to select it, then move, rotate or scale it with gizmos while the image keeps
  path tracing. Moved objects carry their lights along and get motion vectors, so the denoisers follow them.
* **Real-time at 4K** 60 fps in 4K fullscreen with the default scene: about 4 ms of GPU time per frame on an RTX 5090
  (DLSS Quality, 1440p path tracing, ~4 M displaced triangles). See *Performance* below.

---

## 🔧 Building

**Requirements**

* Windows, Visual Studio 2022 (v143 toolset, C++20)
* [Vulkan SDK](https://vulkan.lunarg.com/) 1.4+ installed with the **SDL2** and **GLM** components. The project locates
  everything (headers, libraries and the `slangc` shader compiler) through the `VULKAN_SDK` environment variable that the installer sets.
* A GPU with hardware ray tracing (`VK_KHR_ray_tracing_pipeline`) and mesh shader (`VK_EXT_mesh_shader`) support.

**Steps**

1. Open `FoxVulkanPathTracer.sln` and select the `x64` platform (`Debug` or `Release`).
2. Build. The Slang shaders in `FoxVulkanPathTracer/Shaders` are compiled to SPIR-V as part of the build
   (`Shaders/compile_shaders_slang.bat` does the same by hand).
3. Run from Visual Studio. Assets are loaded relative to the project directory (`FoxVulkanPathTracer/`),
   which is the default debugger working directory. `SDL2.dll` is picked up from the Vulkan SDK's `Bin` folder on `PATH`.

`Debug` builds enable the Vulkan validation layers and print warnings and errors to the console.

Dear ImGui 1.93.0 (MIT license) is included in `ThirdParty/imgui`: only the files the build uses, the core and the
SDL2 and Vulkan backends.

**Optional: DLSS Ray Reconstruction**

The DLSS denoiser is compiled in automatically when NVIDIA's DLSS SDK is found in `ThirdParty/DLSS` next to the solution:

```bash
git clone --depth 1 --branch v310.9.16 https://github.com/NVIDIA/DLSS.git ThirdParty/DLSS
```

The SDK is not part of this repository (it is listed in `.gitignore`) and is covered by NVIDIA's own license (`ThirdParty/DLSS/LICENSE.txt`).
The build copies `nvngx_dlssd.dll` next to the executable. At runtime it needs an RTX GPU and a current driver;
if DLSS is unavailable the renderer says so on the console and starts with SVGF instead.

### Splash screen

While the engine starts, a splash screen shows the Fox Engine logo (`Textures/Fox Engine Logo.png`), a line saying what
is loading (a model, its displacement, the trees' level of detail, the pipelines) and a moving bar. It is a window of its
own on a thread of its own (`Platform/Splash.cpp`, Windows), so it keeps drawing while the main thread loads. The main
window stays hidden until loading is done, and the splash fades out once the first frame is drawn. It can be dragged aside.

The editor matches it (`Editor/EditorTheme.cpp`): the splash's deep navy for panels, the logo's blue glow for what is
hovered and selected, the orange of its letters for check marks, active sliders and tab underlines, rounded corners, and
Segoe UI as on the splash (ImGui's own font where Segoe UI is missing).

### Displacement mapping

Displacement is applied while a model loads (`Graphics/Geometry/Displacement.h`) and is switched on per model (`"displacement": true` in
the scene file, or the *Displacement from normal maps* checkbox when adding a model); height map files are assigned in
`MainRayTracingScene::LoadModel`. The glTF format has no displacement map slot, so the height source of each material is:

1. **A height map file**, assigned by glTF material name:
   ```cpp
   settings.heightMaps["stone_walls.003"] = { "Textures/stone_walls_height.png", 0.05f /* depth in meters */, 0.5f /* midlevel */ };
   ```
2. **Otherwise its normal map**: the relief the normal map describes is reconstructed by integrating its slopes
   (Frankot–Chellappa, in the frequency domain). This is what the cabin uses, as its file ships without height maps.

Main settings:

| Setting | Default | Meaning |
| --- | --- | --- |
| `targetEdgeLength` | 0.03 | Finest triangle edge length in meters |
| `maxTriangles` | 4 000 000 | Budget for the displaced surfaces of one model. It goes where it buys the most visible relief |
| `derivedStrength` | 1.0 | Multiplier for reconstructed heights (1 = what the normal map describes) |
| `derivedMaxHeight` | 0.06 | Cap for reconstructed heights in meters |

More triangles give finer relief but cost load time (about 3.5 s for the cabin's 4 M) and memory; the frame rate hardly
changes (8 M triangles trace within ~0.1 ms of 4 M). Heights are prefiltered to the triangle size, so a coarse
tessellation shows smooth large-scale relief instead of noise.

### Render pipelines: real-time path tracing and the hybrid path tracer

Two pipelines render the same scene with the same materials and lights (**Render > Pipeline**, saved to
`Settings/Render.json` as `"pipeline"`):

* **Real-time path-tracing** (the default): every ray is traced, the camera's too. One ray generation shader traces,
  shades and bounces each path (a megakernel), then DLSS Ray Reconstruction (or SVGF) denoises and upscales.
* **Hybrid Path Tracer**: the first hit of each pixel is rasterized, Nanite style. Lights, shadows and bounces are
  still traced, and the first bounce first looks in the previous frame's image (screen traces, Lumen style).

**The raster: Nanite-style first hits** (`Graphics/Vulkan/Raytracing/HybridRasterPass`, `Shaders/hybrid_*.slang`):

* **Clusters:** every bottom level structure's submeshes are cut into clusters of up to 128 triangles, each with a
  bounding sphere (`RayTracingScene::MakeRasterContent`). This happens again whenever the scene's instances or
  structures change.
* **Instance and cluster culling, on the GPU:**
  * Instances outside the view frustum go first, then each remaining cluster is tested against the frustum.
  * Trees use cluster level of detail. The raster makes its own cut through their cluster graph for its own pixels
    (the rays' cut is chosen for the display's pixels), so trees cost far fewer triangles. The two cuts are less than
    a render pixel apart in error.
* **Two-pass occlusion culling:**
  * **Phase 1** tests clusters against the previous frame's depth pyramid (HiZ), using where each cluster was then.
    What was visible is drawn. What was hidden becomes a candidate.
  * **Phase 2** tests the candidates against the depth pyramid of what phase 1 drew, and draws whatever came into view.
* **Visibility buffer:** each pixel stores only which cluster and which triangle it sees, with reversed depth.
  * **Draws:** visible clusters go into buckets by size (up to 32, 64, 128 or 256 triangles). Each bucket is one
    instanced indirect draw, so a frame is 16 draws instead of hundreds of thousands.
  * **Vertices:** the vertex shader reads the scene's own index and vertex buffers, the very data the rays hit.
  * **Alpha:** alpha tested clusters (leaves) test alpha as the rays' any-hit shader does.
* **Software rasterizer:** clusters narrower than 32 pixels on screen are rasterized in a compute shader instead of by
  the hardware. Their triangles are a pixel or so, and hardware triangle setup and shading of 2×2 pixel quads are
  wasted on them.
  * Each thread covers one triangle's pixel centres. It keeps the nearest with a 64-bit atomic max: depth in the high
    32 bits, cluster and triangle in the low 32.
  * A resolve merges the result with the hardware's visibility buffer, by depth.
* **Shading:** the path tracer reads the visibility buffer, meets the pixel's jittered camera ray with that triangle,
  and describes the surface with the same function as the closest hit shader (`Shaders/surface.slang`). The G-buffer,
  motion vectors, picking and everything after are the same as in real-time path tracing.

**Screen traces: the Lumen-style bounce.** The bounce from each first hit marches through this frame's depth (24 steps
in screen space). Where it goes behind a surface there, it takes that surface's light from the previous frame's
denoised image, reprojected to where the point was, and the path ends. Rays that leave the screen, pass behind
everything or reach a light (emissive surfaces are already counted by light sampling) are traced instead. The light
it takes carries that pixel's own bounces too, so light keeps bouncing over the frames.

**Options** (**Render > Pipeline**, saved to `Settings/Render.json`): `screenTraces`, `occlusionCulling`, `rasterLod`
(the trees' own cut for the raster) and `softwareRaster`, all on by default.

**What it shows:**
* **GPU Profiler sections:** **raster setup**, **cull**, **cull trees**, **raster** (phase 1, hardware and software)
  and **occlusion** (pyramid, phase 2, pyramid).
* **Hybrid raster section** of the window: triangles rasterized (and of them in software), clusters drawn in each
  phase, clusters tested, outside the view and hidden.
* **Console:** the same as a `[Hybrid]` line.

**Measured** on an RTX 3070 Laptop GPU (Performance preset, 427×280 rendered, DLSS to 1280×840):

| Scene | Real-time: trace | Hybrid: cull + raster + occlusion, trace (screen traces on / off) |
| --- | --- | --- |
| my_yard (32 000 instances in view, trees and grass) | 8.7 ms | 7.5 ms + 9.0 ms / 7.8 ms + 7.7 ms |
| cloth test scene | 0.7 ms | 0.1 ms + 2.0 ms / 0.1 ms + 1.0 ms |

What the raster optimizations did on my_yard:
* **Level of detail:** the raster's own tree cut took it from 34 to 22 million triangles.
* **Buckets:** 230 000 draws became 16.
* **Software rasterizer:** 19.6 million of the 22 million triangles now go through it, and the raster went from
  13 ms to about 4.5 ms.

Even so, on this GPU the hardware ray tracer is the faster pipeline for these scenes. Camera rays cost it very little,
thanks to opacity micromaps and shader execution reordering. Screen traces cost more than the rays they save here: a
hardware ray in a simple scene is cheaper than a 24 step march. The hybrid pipeline pays off where tracing is
expensive, on GPUs with slower ray tracing, or for many paths per pixel.

**Not handled:**
* Only trees have level of detail: other meshes are drawn in full, every cluster that passes.
* Clusters are not culled by facing: the path tracer shows both sides of every surface.
* Vertices that move (skinned without cluster level of detail, simulated cloth, water and particles, sculpted terrain)
  have no bounds of their own, so they are drawn whenever their instance is.
* The raster's buffers take about 100 MB of video memory even in real-time path tracing (the path tracer's
  descriptors always point at them).
* Screen traces see only the previous frame's view-dependent image, and nothing off screen.

### Performance

**Render presets** (**Render > Preset**, saved to `Settings/Render.json`) set how hard the path tracer works:

| Preset | Resolution | Bounces | Frame rate | Wind level of detail |
| --- | --- | --- | --- | --- |
| Quality | Native (Quality upscaling at 4K and above) | 4 | As vsync allows | Off |
| Balanced | DLSS Balanced (58 % of the display's width and height) | 3 | 60 fps at most | On |
| Performance | DLSS Performance (50 %) | 2 | 40 fps at most | On |
| Laptop 30 fps | DLSS Performance (50 %); Ultra Performance (33 %) above 1200 display lines | 2 | 30 fps at most | On |
| Laptop 25 fps | DLSS Ultra Performance (33 %) | 1 | 25 fps at most | On |

* **Laptop 30 fps and Laptop 25 fps** are for keeping a laptop cool and quiet: the GPU rests for a larger part of
  each frame. Laptop 25 fps also renders a ninth of the pixels and traces one bounce.
* **CPU time:** `FOX_CPU_PROFILE=1` prints where each frame's CPU time goes (UI, scene update, command recording) every
  2 seconds. In my_yard the UI used to take 86 ms a frame (10 fps whatever the GPU did): every text read its font
  file again each frame, and the world canvases were drawn pixel by pixel on the main thread. Fonts now stay loaded,
  and canvas pictures are drawn on other threads, a row span at a time, and uploaded a frame later.
* **Auto** (the default) picks **Balanced** on laptop GPUs (a name with "Laptop"), integrated GPUs and GPUs with less
  than 10 GB of their own memory, and **Quality** on the others.
* **The frame cap** keeps a laptop's GPU cooler, so it keeps its clocks: uncapped, an RTX 3070 Laptop reached 86 °C
  and fell to 210 MHz, and every pass took two to three times longer. Performance caps at 40 fps so the GPU rests
  between frames even in heavy scenes (my_yard needs about 15 ms per frame there).
* **Wind level of detail**:
  * **How often a tree mesh gets new wind** depends on its size on screen, measured at its nearest instance:
    * every frame from 600 pixels;
    * every 2nd frame from 150;
    * every 4th frame from 40;
    * never below that: it stands still. A growing tree never stands still.
  * **Skipped frames:** the time a tree missed is added at its next update, since the springs split it into small steps. In between, the tree keeps its shape and reports no motion, so the denoiser doesn't smear it.
  * **Cost:** the skinning shader now writes only what changes, not whole vertices, and doesn't write the refit input for cluster-traced trees.
  * **Measured** in my_yard, with the laptop on battery at the same 40 W cap before and after: skinning went from 22.5 ms to 12.8 ms.
  * **GPU Profiler:** the "Trees in the wind" row shows how many tree meshes are at each rate.
* **Picking a resolution by hand** (`U`) overrides the preset's until a preset is chosen again.

On an RTX 3070 Laptop at 1280 x 840 (default scene, DLSS Ray Reconstruction, GPU time per frame):

| Preset | Trace | Denoise | Frame |
| --- | --- | --- | --- |
| Quality | 7.9-8.5 ms | 11.5-11.7 ms | 21.1-21.6 ms |
| Balanced (Auto there) | 2.1 ms | 3.1 ms | 6.1 ms |
| Performance | 1.8 ms | 3.1 ms | 5.8 ms |

Balanced looks nearly the same as Quality there (the finest leaves a little softer), at less than a third of the
cost. Performance gains little more, because DLSS takes about the same time below that size.


Measured in 4K fullscreen on an RTX 5090 (default scene, DLSS Ray Reconstruction, 1 path per pixel):

| Upscaling mode (`U`) | Render resolution | GPU time | Uncapped frame rate |
| --- | --- | --- | --- |
| Native | 3840×2160 | 8.6 ms | ~116 fps |
| **Quality** (default at 4K) | 2560×1440 | 4.4 ms | ~225 fps |
| Performance | 1920×1080 | 2.9 ms | ~340 fps |

What makes it fast:

* **Scene data in video memory.** Vertices, indices, materials, lights and the environment sampling table are uploaded
  to device-local buffers. Every ray hit reads them; from host-visible memory (system RAM, over PCIe) the trace was ~15× slower.
* **Shader execution reordering** (`VK_NV_ray_tracing_invocation_reorder`, RTX 40 series and newer): bounce rays are
  regrouped by what they hit before shading (`raygen_ser.spv`, chosen automatically; other GPUs use `raygen.spv`). ~15% faster tracing.
* **Frame pacing.** Each frame is two submits: tracing and denoising start at once, and only tone mapping waits for a
  swapchain image. Fullscreen is exclusive, which bypasses the desktop compositor (in a borderless window Windows
  dropped every few frames even with GPU time to spare).
* **Path tracer**: one shadow ray per bounce, inline ray queries for shadows, the primary hit reused by the G-buffer and
  the path, four bounces with Russian roulette, instanced acceleration structures.

The window title shows the GPU time per pass (trace, denoise, tone map). With vsync on (the default) the frame rate is
capped at the display's refresh rate; press `V` to turn vsync off and see the real throughput.

### Glass

A material with `transmission` > 0 is glass, tinted by its base colour. It comes in two kinds (`Graphics/Vulkan/Material.h`):

| Kind | Set by | Behaviour |
| --- | --- | --- |
| Thin walled (window panes) | `ior = 0` (default) | Light passes straight through |
| Solid | `ior > 0`, e.g. 1.5 | Light refracts into and out of the solid and is absorbed inside |

```cpp
Fox::Graphics::Vulkan::Material glass;
glass.roughness = 0.0f;        // > 0: frosted glass
glass.transmission = 1.0f;
glass.ior = 1.5f;              // Water 1.33, diamond 2.42
const float color[3] = { 0.80f, 0.94f, 0.90f };
glass.SetAttenuation(color, 2.0f); // The colour light keeps after 2 m inside the glass
AddSphere(center, radius, glass);
```

glTF models get solid glass from `KHR_materials_volume` (a `thicknessFactor` above 0) together with
`KHR_materials_transmission`. Solids must be closed meshes whose normals point outwards.

Limitations: light is not focused through glass (no caustics). The sun's light passes through solid glass straight,
tinted and absorbed, so a glass ball casts a light, coloured shadow rather than a bright focal spot. SVGF denoises the
glass surface itself rather than the view through it (the view through a curved solid changes too much with the
sub-pixel jitter for its history), so solids look softer with SVGF than with DLSS.

### Skinning and the tree in the wind

`RayTracingScene::AddSkinnedMesh` adds a mesh whose vertices follow joints: each vertex has up to four joints and
weights, and `SetJointMatrices` gives each joint's `pose * inverse(bind)` matrix. Every frame a compute shader
(`Shaders/skinning.slang`) places the vertices, the mesh's acceleration structure is refitted and the top level one
rebuilt. The shader also keeps where each vertex was in the previous frame, so the motion vectors follow the
deformation and the denoisers do not smear moving branches.

The tree (`Graphics/Geometry/TreeGenerator`) grows as chains of tapered cylinders batched into one mesh: the trunk,
branches out of it, and smaller branches out of those. Each cylinder gets a bone as it is built, parented to the
previous cylinder (or, for a branch's first cylinder, to the one it grows from). The ring where two cylinders meet is
weighted half and half to both bones, so the wood bends smoothly at the joints.

Wind (`Animation/WindSkeleton`) treats every joint as an angular spring:
`I * angular acceleration = wind torque - k * angle - c * angular velocity`.

* Wind torque: drag on every branch segment from the wind's flutter (across the wind, up and down, a little along
  it), linearised about the gusty wind speed, summed over everything the joint carries, about the joint.
* Inertia: of that subtree about the joint. The stiffness `k` follows from a natural frequency that rises from
  0.45 Hz for the trunk to a few Hz for twigs. Damping is a fraction of critical.
* Gusts: roll across the scene with the wind.

**One wind.** The direction is the scene's (Environment window > **Wind towards**, saved with the scene), the same for
every tree. Instances of a tree share one rig but each has its own rotation, so a downwind bend computed in the mesh
would point every rotated instance a different way. So the rig only flutters, with no direction of its own. The steady
push downwind is each instance's **lean** (`WindSkeleton::UpdateLean`): a damped spring at the trunk's frequency that
tilts the whole instance about its base towards the wind, about 4 degrees at 8 m/s and more in gusts. It costs nothing
on the GPU: only the instance's transform in the top level structure changes. Speed, gusts, response and damping stay
per tree (Trees window > Wind).

Settings such as wind speed, gust strength, damping and response are in `WindSkeleton::Settings`; tree shape in
`TreeSettings`.

**Growing.** A tree can grow from a sprout (`Animation::TreeGrowth`, Trees window > Growth). It grows through the same
rig the wind moves:

* **Branches:** a growth front moves out from the root along the bones at a constant speed. Each bone scales up from
  its base as the front passes it, lengthening and thickening, and carries its subtree with it. So the trunk rises,
  and branches sprout where the front has been and reach out.
* **Size:** meanwhile the whole tree enlarges from **Sprout size** to its full size.
* **Leaves:** each leaf card unfolds from its stem, scaled about it in the skinning shader, when a second front
  reaches it. That front follows the branches by **Leaves follow by** (a fraction of the tree). The generator stores
  each leaf vertex's stem and its distance along the tree (`SkinnedMesh::growth`).

The geometry is generated once: growing costs no more than the wind. The wind keeps blowing while it grows.
**Duration** is from sprout to fully grown. **Loop** starts again once grown, the **Grown** slider scrubs (and
pauses), and **Play** / **Restart** run it. Play mode (`F5`) grows every growing tree from the start. Instances of a
tree grow together, and the growth settings are saved with the tree.

The bark uses every texture in `Textures/bark`:

| Texture | Use |
| --- | --- |
| `albedo.png` | Base colour (sRGB) |
| `normal.png` | Normal map, OpenGL convention: the tree's tangents are set up so its green channel points along the branch |
| `roughness.png`, `metallic.png`, `ao.png` | Packed when loading into one texture the glTF way: R occlusion, G roughness, B metallic |
| `displacement.png` | Pushes the trunk and branches out along their normals while the tree is generated |

The tree's thicker branches are subdivided to about 3 cm triangles for the displacement, about 80 000 triangles in
all. Their skin weights are kept, so the relief bends with the wind. Heights are averaged down to the triangle size
first, and the normal map adds the finer detail. Branches thinner than 2.5 cm stay smooth. The texture repeats every
meter along a branch, and a whole number of times around it, so there is no seam.

Ambient occlusion (`Material::occlusionTexture`, red channel) darkens light from the surroundings in crevices finer
than the geometry: sky light and light bouncing off other surfaces. Direct sunlight and lamps are not affected, as in
glTF. The tree is an object like the others: move it with the gizmos and the wind keeps blowing in world space.

### Tree editor

In edit mode (`E`) the **Trees** window lists the scene's trees.

* **Add tree** puts a new tree on the ground 12 m in front of the camera. **Duplicate** copies the selected tree's
  settings to a new one, and **Remove** deletes the tree.
* Clicking a tree in the scene selects it in the window. Clicking it in the list selects it for the gizmos.

| Section | What it changes |
| --- | --- |
| Placement | Name, position, turn, scale, stand on the ground (the gizmos do the same) |
| Trunk | Height, radius, taper, segments; the **cross-section** and **curve** editors (below) |
| Branches | Levels, where branching starts, branches per segment, angle, length and thickness ratios, turn towards light |
| Randomness | Seed (and **New** for a random one), plus a separate amount for every random choice |
| Leaves | On/off, which branch level carries them, cards per segment, card size, droop, leaf relief |
| Bark relief | Displacement depth, texture size, triangle size for the relief, radius below which branches stay smooth |
| Materials | Bark and leaf textures of **this tree**, chosen from the images under `Textures/`, plus tint, roughness, alpha cutoff |
| Wind | Direction, speed, gusts, flutter, response, damping, sway frequencies, maximum bend (takes effect immediately) |
| Growth | Grows (from a sprout), how far grown (scrub), play / pause / restart, loop, duration, sprout size, how far the leaves lag |

**Cross-section.** The trunk's rings follow a closed Catmull-Rom spline instead of a circle:

* **Editing:** drag its control points on the canvas. **Add point** inserts one after the selected point, and
  **Remove point** deletes the selected one.
* **Presets:** round, oval, ribbed and buttressed.
* **Blending:** *Shape at base* and *Shape at top* blend between the spline and a circle along the trunk, so a
  buttressed base can round out higher up. *Shape on branches* passes some of the shape on to the branches.
* **Spacing:** vertices are spaced evenly along the outline, and the bark texture repeats a whole number of times
  around it, so there is no seam.

**Curve.** The trunk's centre line is an open spline through horizontal offsets at evenly spaced heights, base to
top:

* **Views:** a front view (x) and a side view (z). Drag a point sideways in either view.
* **Buttons:** **More points** and **Fewer points** resample the curve. **Straight**, **Lean** and **S-curve** are
  presets.
* **Trunk wander:** under Randomness, adds random bending on top of the curve.

**Per-tree textures.** Each tree has its own six texture slots for bark and for leaves (colour, normal, roughness,
metallic, occlusion, displacement):

* **Conventions:** each surface has its own normal-map convention and displacement inversion setting.
* **Loading:** textures load when first chosen and are then shared by every tree that uses them
  (`Scene/TextureLibrary`), including the packed occlusion/roughness/metallic texture.
* **Leaf shape:** the leaf colour's alpha builds the opacity micromaps, and the leaf texture's proportions set the
  card shape.

**How changes are applied.** While a control is being dragged the change waits for its release, unless *Live
update* is on. Then the renderer waits for the GPU and regenerates the trees whose shape changed. With two trees a
rebuild takes about 70–80 ms. It then rebuilds the trees on the GPU only:

* **Static geometry stays:** the scene's buffers are created with room for about 2.5 million tree vertices and 12
  million indices (the bar at the top of the window shows how much is used), so the static geometry is never touched.
* **What is rebuilt:** the tree part of the buffers, the trees' skinning data, opacity micromaps and acceleration
  structures.
* **Micromaps:** classifications are kept between rebuilds, so only the first build classifies micro-triangles.

### Instanced trees and mesh painting

Entities can share one tree: they are **instances** of it. The tree's geometry is generated, stored, deformed by the
wind and given a bottom level acceleration structure once; each instance is one more top level instance with its own
transform, so a forest costs little more than its distinct trees. Instances sway together (the deformation is
shared) and change together: editing the tree in the **Trees** window changes all of them (the window says how many
there are). **Add Instance** in the Trees window and **Duplicate as Instance** (Edit menu or the Hierarchy's
right-click menu) make instances; **Duplicate** makes a tree of its own. Scene files list each tree once under
`"treeAssets"` and tree meshes refer to it by index, so instances stay instances when loaded.

The **Mesh Paint** window (Window menu; `M` turns the brush on) paints models and trees onto the ground, the terrain
or the ground plane:

| Input / setting | What it does |
| --- | --- |
| Left mouse | Paints: fills the brush up to the density |
| `Ctrl` + left mouse | Erases what was painted under the brush |
| `[` `]` | Brush radius (the mesh brush while it is on, otherwise the terrain brush) |
| **Density** | Meshes per 100 m² the brush fills an area with |
| **Min spacing** | No two painted meshes closer than this (0: from the density) |
| **Same tree apart** | Instances of the same tree variant keep this far apart. A spot nearer to one gets another variant; if every variant stands too near, a new one is grown (while there is room for its geometry), and if none can be, the spot stays empty |
| **Meshes** | What the brush paints: models (from the list, or dragged from the Content Browser) and trees (any tree of the scene, or a new one), each checked or not, with a weight (how often it is chosen), a scale range, random rotation and how far into the ground it goes. Models can tilt with the slope. Trees have **Variants**: the same tree grown from other seeds, each one geometry for all its instances |

Painted meshes land on the ground where they are painted: models with their lowest point on it, trees upright with
their base slightly in it. They go under **Painted** in the Hierarchy, one group per mesh (big groups start folded).
While a stroke lasts its new meshes show as dots; the scene rebuilds once when it ends. **Drop to Ground** (Edit menu,
or right-click in the Hierarchy) puts any entity back onto the ground.

Limits: every distinct tree takes room in the space reserved for dynamic geometry (`DYNAMIC_VERTEX_CAPACITY`, 2.5 M
vertices: about half a dozen detailed trees), so variants are few and instances many. Up to 16,384 top level instances:
half for models and spheres, the rest for trees.

### Cluster level of detail (trees)

On GPUs with `VK_NV_cluster_acceleration_structure` (NVIDIA RTX, recent drivers) trees are traced with continuous
level of detail, after Nanite, through cluster acceleration structures (RTX Mega Geometry). `FOX_NO_CLUSTER_LOD=1`
traces them at full detail as before, for comparisons; GPUs without the extension do that anyway.

When a tree is generated, its mesh is cut into clusters of up to 128 triangles (`Graphics/Geometry/ClusterLod`, with
[meshoptimizer](https://github.com/zeux/meshoptimizer), vendored in `ThirdParty/meshoptimizer`). Neighbouring
clusters are grouped and each group is simplified to about half its triangles with its border locked, then cut into
new clusters: the next level. Repeating that builds a graph from the full tree (about 2 200 clusters, 310 000
triangles) up to a single cluster, in about 0.2 s. Each group records the error its simplification made and a bounding
sphere. Leaf cards are not simplified that way (it would wreck their cutouts): their levels keep every other card and
enlarge the kept ones so the canopy covers as much.

Every frame (`Graphics/Vulkan/Raytracing/ClusterLodPass`, `Shaders/cluster_lod.slang`):

1. Each instance picks its clusters: those whose own error is under the threshold seen from the camera while their
   group's is not. Groups share their error and bounds and errors grow up the graph, so the picked clusters always
   cover the tree once, without cracks. The CPU narrows the candidates per instance to the levels its distance can use.
2. Every cluster some instance picked is instantiated from its template (built once, in the bind pose) with its
   vertices as the wind left them this frame. A cluster is built once however many instances use it.
3. Each instance gets a bottom level structure made of its clusters' structures (no triangles: fast), patched into
   its top level instance before the top level build.

Hit shading needs no changes: each cluster is a submesh, the instance's custom index its tree's first cluster and
the geometry index the cluster. **Render > Tree Detail** sets the error allowed, in pixels of the displayed image (1 by
default; larger is coarser and faster). The window title shows how many clusters were built and traced, and the GPU
times break the scene update down (skin, lod select, clusters, instances, tlas).

Measured on an RTX 3070 Laptop with a painted forest of 238 trees (7 variants), same view, 1280x840:

| | Full detail | Cluster level of detail |
| --- | --- | --- |
| Skinning (+ refits) | 25 ms | 4 ms |
| Selection, cluster and instance structures | | 7 ms |
| Trace | 99 ms | 42 ms |
| GPU frame | 144 ms | 63 ms |

Limits: the cluster structures take memory: about 525 MiB for those 7 variants (templates, a structure for every
cluster there is, and instance structures for up to 524 288 clusters; past that budget every tree gets coarser for a
while). Clustered leaves are alpha tested by the any-hit shader, without opacity micromaps. The level of detail is
chosen for the camera: reflections and shadows see the same clusters.

### Water

**Create > Water** makes an entity with a Water component: a rectangle of water centred where the entity is, filled
up to its height and then left to flow. It lies on the ground (the terrain or the ground plane, sampled when it is
placed): it fills hollows, runs down slopes and leaves dry land dry, so the shore is wherever the water meets the
ground. On flat ground it stands as a sheet whose edges show; in a terrain basin it makes a lake.

**Motion** (`Scene/WaterSimulation`, `Shaders/water.slang`): the shallow water equations, the Navier-Stokes equations
averaged over the depth, on a staggered grid on the GPU. Each step the velocity is carried along by itself
(semi-Lagrangian advection), spread by viscosity, pushed down the slope of the surface by gravity (the pressure
term), slowed by bed friction and driven by the wind's stress; then each cell's depth changes by what flows in and
out, with each cell's outflow limited to what it holds. Steps are small enough for waves (sqrt(g h)) to cross at most
a third of a cell, so a frame takes a few. The grid's edges are walls. Rain drops, the wind's gusts and clicks in edit
mode (with **Splash where clicked** on) add dips with a ring around them, which the water carries away as waves. The
volume is read back every second and what rounding took is put back, so the water level holds.

**Look**: water is a dielectric with an index of refraction of 1.333. The path tracer reflects and refracts it by
Fresnel and absorbs light along each path through it (**Colour in depth** after **Clarity** metres: shallow water
looks clear, deep water blue-green), and the sun lights the bed through it. The wind's ripples finer than the
simulation's cells (1.5 cm to 60 cm, `Shaders/water_waves.slang`) are added to the shading normal: waves spread
around the wind, each travelling at its own speed (omega^2 = g k + (sigma / rho) k^3: capillary and gravity waves),
the short ones rising in a light breeze and the long ones with more wind. Waves too fine to show at the distance turn
into roughness instead of flickering. The surface keeps where it was a frame ago, for motion vectors.

| Setting | What it does |
| --- | --- |
| Size, Cell size | Extent and resolution (at most 512 cells a side); changing them places the water anew |
| Colour in depth, Clarity | Absorption: the colour white light keeps after that many metres |
| Roughness, Ripples | Micro roughness of the surface; strength of the fine wind ripples |
| Wind, Wind towards, Gusts | The wind: its stress drives the flow, its gusts ruffle the surface, its ripples shade it |
| Rain, Drop size, Drop strength | Raindrops per square metre and second |
| Viscosity, Bed friction | How quickly motion smooths out, and how much the bed slows it (shallow water more) |
| Foam, Foam lifetime | How much foam breaking water makes; seconds for it to fade to a third |
| Spray, Droplet size, Droplets in flight | How many droplets are thrown up, their radius, and room for them at once (0: none) |

**Foam** is a field on the grid, made where the water breaks or churns: steep wave faces (about to break), converging
flow (wave fronts, bores), water running against the shore, fast turbulent water, drops and splashes, and spray falling
back. The current carries it (semi-Lagrangian) and it fades as its bubbles burst. Each surface vertex takes its amount;
the closest hit shader turns it into coverage, patchy streaks over noise drifting with the wind (a little foam is a few
specks, a lot a sheet), and where it covers the water is white, rough and opaque: a froth scatters light every way.

**Spray** is droplets thrown up where foam is made, faster where the water breaks harder, and torn off the crests by
wind above 9 m/s (spume). They fly under gravity and quadratic air drag towards the wind (the small ones drift
furthest) and land back as foam. Each droplet is a small octahedron in a second surface whose acceleration structure is
built anew every frame; droplets not in flight have NaN corners, which the build skips. Spray is white: many droplets
smaller than a pixel scatter light between them (and clear glass that small would trap rays by total internal
reflection until they ran out of bounces, and look black).

Moving the entity places the water anew once it stops (the simulation starts over). `FOX_WATER_DEBUG=1` logs the
depth range, volume and steps every second.

Limits: a vertical wave front cannot break (the equations have one height per point: breaking shows as foam and spray
instead), spray does not collide with objects, and light refracted through the surface is not focused into caustics on
the bed.

### Volumetrics: fog, clouds and smoke

The path tracer scatters light in participating media (`Shaders/volumes.slang`). They are lit by the sun, the lamps
and the sky, shade what is behind them, and cast shadows:

* **Fog** (Environment window, or **Create > Effects > Fog**): height fog, thickest at its **Base height** and
  thinning exponentially above it (**Height falloff**). Its optical depth along a ray has a closed form, so where a
  ray meets it is sampled exactly, and fog costs no marching.
* **Clouds** (Environment window, or **Create > Effects > Clouds**): a layer of the sky between **Bottom** and
  **Top**, shaped by noise that drifts with the **Wind**. **Coverage** sets how much of the sky they fill. They shade
  the ground (**Cast shadows**).
* **Smoke** (**Create > Effects > Smoke** and **Glowing Smoke**, or a **Volume** component on any entity): a puff (or
  box) of **Size** metres, turned and scaled with its entity. Noise eats ragged edges, wisps and gaps into it, and
  drifts through it (**Drift**: rising smoke). It can glow (**Glow**: fire, embers), lighting what is around it
  through the paths that meet it. At most 8 render at a time; the selected one shows its box.

How it works:

* **Finding the media:** on every path segment the media are sampled for where the ray first meets them, each on its
  own, and the nearest wins. Fog is sampled analytically. Clouds and smoke are marched in a fixed number of jittered
  steps (fewer on later bounces and for shadow rays). Empty sky is skipped after the clouds' first noise octave.
* **Scattering:** where a path meets a medium it scatters (Henyey-Greenstein, forward scattering set per medium).
  Light is sampled there as at a surface, with the phase function in place of a BSDF and shadow rays that pass
  through the media. Colour is the single scattering albedo: white scatters, dark absorbs.
* **Multiple scattering in clouds:** paths stop after a few volume bounces, which would leave the inside of thick
  clouds black. So light reaching a point in a cloud sums octaves of weaker extinction and wider scattering (as in
  Wrenninge et al., "Oz: the great and volumetric").
* **Fog bounces:** fog scatters on once; after that only its direct light is gathered.
* **Denoising:** clouds, smoke and dense fog seen against the sky are described to the denoiser as a surface where
  they begin, with their opacity along the ray as its albedo. The light in them is smoothed, and their edges and
  wisps stay sharp.

Media are grey (the same extinction for every colour), and smoke volumes do not act as light sources for light
sampling (glow is found only by the paths). The cost depends on how much of the view they fill. Measured at 1280 x 840
on a laptop RTX 3070 (the trace pass, before the GPU heated up and throttled): 7 ms without media, about 15 ms with
fog everywhere, 20 to 26 ms with clouds.

### Audio

Sound and music play with [miniaudio](https://miniaud.io) (v0.11.22, public domain / MIT-0, vendored in
`ThirdParty/miniaudio`). Files are mp3, wav or flac, kept in `Sounds/`. Short files are decoded once and kept, long
ones (above 1 MB) are streamed. There are two buses under the master volume, **music** and **effects**, with their
volumes in the Environment window's **Audio** section. The camera is the listener.

**Sound component** (Inspector > Add Component > Sound, or **Create > Audio > Sound** / **Background Music**):

* It plays a file while the game plays: **Play when the game starts**, or when a script asks.
* **In the world (3D)**, it is heard from where its entity is and follows it, at full volume within **Full volume
  within** metres and quieter until **Fades out to**.
* **Background music** makes it the game's music instead: one track at a time, the same everywhere, and a new one
  crossfades.
* **Loop** repeats it; otherwise it plays once.
* **Volume** and **Pitch** set how loud and how high it plays.
* **Preview** plays it in edit mode too.

**Scripts** play sounds on events:

```csharp
public override void OnTriggerEnter(Entity other)
{
    hit = Audio.PlayAt("Sounds/damn_mad_fox.mp3", Entity.Position, volume: 1.0f, pitch: 1.25f); // From here
}
public override void OnTriggerExit(Entity other) => hit.Stop(0.5f);   // Fades out over half a second

Audio.PlayMusic("Sounds/damn_mad_fox.mp3", loop: true, volume: 0.5f, fadeSeconds: 1.5f);   // Background music
Audio.PlayMusic("Sounds/damn_mad_fox.mp3", loop: false);                                  // Once
Audio.StopMusic(fadeSeconds: 1.0f);
Entity.Sound.Play();                                                  // Its Sound component
```

| | |
| --- | --- |
| `Audio` | `Play(file, volume, pitch, loop)` (the same everywhere), `PlayAt(file, position, volume, pitch, loop, minDistance, maxDistance)`, `PlayMusic(file, loop, volume, fadeSeconds)`, `StopMusic(fadeSeconds)`, `MasterVolume`, `MusicVolume`, `EffectsVolume` |
| `Sound` (what they return) | `IsPlaying`, `Stop(fadeSeconds)`, `Volume`, `Pitch`, `Position` |
| `Entity.Sound` | Its Sound component: `Play()`, `Stop()`, `IsPlaying`, `File`, `Volume`, `Pitch`, `Loop` |

Pausing the game (`F6`) pauses its sound, and stopping play stops it all. In `Scenes/scripting-demo.json`:

* `MusicPlayer` plays background music, looping (its **loop** field off: once). The Interact action (F) stops it and
  starts it again.
* `GoalZone` plays a sound from the zone while the player is in it.

Without an audio device the engine says so on the console and runs silent. `Audio/AudioEngine` keeps miniaudio, and
the `windows.h` it brings, to itself.

### Prefabs

A prefab is an entity and everything under it saved as a file (`Prefabs/<Name>.prefab.json`), placed in scenes any
number of times. Each placement (an instance) can differ from the prefab: its **overrides**.

* **Make one:** right-click an entity in the Hierarchy > **Create Prefab** (or **Edit > Prefab > Create Prefab**). The
  entity becomes its first instance.
* **Place one:** **Create > Prefab > <name>**, in front of the camera. Instances show blue in the Hierarchy, their
  prefab's name after them; what is under an instance is a paler blue.
* **Override:** just edit an instance (or anything under it) in the Inspector. Whatever differs from the prefab is an
  override of that instance only: a component's field (a sphere's radius, a material, a light's colour), whether
  an entity is active, a component added or removed. The instance's **Prefab** section lists its overrides, each
  with **revert**.
* **Apply to prefab:** the instance as it is becomes the prefab. Every other instance updates, and keeps its own
  overrides: change the prefab's material and the instance with a bigger radius gets the new material at its
  bigger radius.
* **Revert all** takes the instance back to the prefab. **Unpack** unlinks it: what is under it stays, as ordinary
  entities.

An instance's transform and name are always its own. Overrides are found by comparing the instance with its prefab
field by field (`MainRayTracingScene::GetPrefabOverrides`), so no edit has to be recorded as one. A field that holds
a whole object (a material) is one override. Entities are matched by their path of names under the instance
(`Wheel/Hub`). Things added under an instance are not kept by it, so add them to the prefab (edit an instance,
then Apply).

Scenes save an instance as its prefab file, its transform and its overrides:

```json
"components": {
  "transform": { ... },
  "prefab": { "file": "Prefabs/Glass_ball.prefab.json", "overrides": { "|mesh|radius": 0.6 } }
}
```

When the scene loads, every instance is made from its prefab with its overrides on top. Prefabs can contain
instances of other prefabs (8 deep at most). A prefab file is read again when it changes on disk. Its trees are
shared by all its instances: one geometry, like tree instances.

### Play mode

**Play** (`F5`, the toolbar above the view, or the **Play** menu) runs the game on a copy of the scene. **Stop** puts
the scene back exactly as it was when play started, whatever the game or you did meanwhile.

**Game time** drives everything that moves while it plays: physics, agents, growing trees, water, wind, and the drift
of clouds and smoke.

* **Pause** (`F6`) freezes it.
* **Step** (`F7`) advances one frame of 1/60 s, paused.
* **Speed** (the toolbar's slider, or **Play > Speed**) runs it from a standstill to 16 times as fast.
* A long frame (a hitch, a breakpoint) counts as at most 0.1 s, so nothing tunnels or explodes after it.

The toolbar shows the game time and frame.

**Systems run in a fixed order** every frame (`MainRayTracingScene::Update`):

1. Transforms
2. Agents
3. Physics (fixed 1/60 s steps)
4. The renderer's copy of meshes, lights and water
5. Animation (water simulation, wind, growth)

Entities given a Physics component while it plays (created by hand, or by the game later) join the simulation at once.

**Game camera.** An entity with a **Camera** component (**Create > Game Camera** makes one where the view is, looking
the same way) is what the game looks through while it plays. It looks along its -Z axis, with its own field of view;
of several, the enabled one with the highest **Priority** wins.

* **Free camera** (`F8`, or the toolbar's camera button) lets go of it, to fly around the running game, and `F8` takes
  it again.
* Stopping returns the editor's view to where it was before play.
* The selected camera shows where it looks.

**The copy.** When play starts, the scene is saved in memory (the same JSON as a scene file) along with every
component's values. When it stops:

* **Quick restore:** if the same entities are there with the same kinds of components, every value goes back in
  place: transforms, names, physics, agents, lights, water settings, smoke, cameras, whether meshes show. Instant.
* **Full restore:** otherwise (entities created or deleted, meshes changed, anything that rebuilt the scene), the
  whole scene is restored from the saved copy, with one rebuild (about a second). Its trees are the same trees, so
  they are not generated again.

Saving while it plays saves the scene as it was when play started; loading a scene stops play without restoring.

### Scripting (C#)

Gameplay is written in C#: classes deriving from `Fox.Script` in `Scripts/*.cs`, run while the game plays. Give an
entity a **Script** component (Inspector > Add Component > Script, then **Add script...**, or **New script file** to
start one from a template). `Scenes/scripting-demo.json` has examples at work:

* `PlayerBall` rolls a ball with the MoveX / MoveY axes (W A S D), jumps with Jump (Space), and drops boxes with Drop (B).
* `FollowCamera` follows it with the game's camera.
* `Chaser` sends an agent after it.
* `Spinner` turns and bobs a box.

```csharp
using System.Numerics;
using Fox;

public class Spinner : Script
{
    public float degreesPerSecond = 90.0f;   // Public fields show in the Inspector, saved with the scene

    public override void OnUpdate(float deltaTime)
    {
        Entity.Rotate(Vector3.UnitY, degreesPerSecond * deltaTime);
        if (Input.GetKeyDown(Key.Space) && Scene.Raycast(Position, -Vector3.UnitY, 2.0f, out RaycastHit hit, Entity))
            Entity.Physics.AddImpulse(Vector3.UnitY * 5.0f * Entity.Physics.Mass);
    }
}
```

**Life:** `OnStart` once before the first update (when play starts, or when the script is added while playing), then
`OnUpdate(deltaTime)` every frame of game time (not while paused; scaled by the game speed). `OnStop` runs when play
stops or the script's entity goes. Scripts run first in the frame, before agents and physics.

**The API** (`Scripting/FoxScripting/Engine.cs`; vectors are `System.Numerics`):

| | |
| --- | --- |
| `Entity` | `Name`, `Active`, `Parent`, `Children`; `Position` / `Rotation` (world), `LocalPosition` / `LocalRotation` / `Scale`; `Forward` / `Right` / `Up`, `Translate`, `Rotate`, `LookAt`, `AimAt` (cameras); `Destroy`, `Duplicate`; `Has` / `Add` / `Remove` components; `GetScript<T>`, `AddScript`; `Entity.Find(name)`, `Entity.Create(name)` |
| Components | `Entity.Physics` (`Velocity`, `AngularVelocity`, `Mass`, `AddImpulse`, `AddForce`, `Layer`, `IsTrigger`), `Entity.Agent` (`Target`, `Speed`, `Arrived`, `Stop`), `Entity.Light` (`Color`, `Intensity`, `Enabled`), `Entity.Mesh` (`Visible`), `Entity.Camera` (`FieldOfView`), `Entity.Volume` (`Density`, `Color`, `Glow`) |
| `Scene` | `Spawn("ball" / "box" / "sphere" / "smoke" / "glowing smoke" / "agent" / "empty" / a model file, position)`, `Find`, `Raycast(origin, direction, maxDistance, out hit, ignore)` (physics bodies and the ground), `GameCamera` |
| `Physics`, `Layers` | Queries (`Raycast`, `RaycastAll`, `SphereCast`, `BoxCast`, `CapsuleCast`, `OverlapSphere`, `OverlapBox`, `OverlapCapsule`) with layer masks (`Layers.Mask`, `NameToLayer`); events: `OnCollisionEnter` / `Exit`, `OnTriggerEnter` / `Exit` (see *Physics*) |
| `Input` | Named input: `GetAction` / `GetActionDown` / `GetActionUp("Jump")`, `GetAxis("MoveX")`, `GetVector("MoveX", "MoveY")` (see below); the keys themselves: `GetKey` / `GetKeyDown` / `GetKeyUp` (`Key.W`, `Key.Space`, ...), `GetMouseButton...`, `MouseDelta`, `MousePosition` |
| `Time` | `Delta`, `Elapsed`, `Scale`, `Frame` |
| `Debug` | `Log`, `Warn`, `Error` (to the console) |

**Input actions.** Scripts ask for what the player means rather than which key it is:

* **Actions** are buttons: `Input.GetActionDown("Jump")`.
* **Axes** run from -1 to 1 between a negative and a positive key: `Input.GetAxis("MoveX")`.
  `Input.GetVector("MoveX", "MoveY")` gives a direction no longer than 1, so diagonals are not faster.

The **Input Actions** window (Window menu) maps them to keys and mouse buttons:

* **Add a key:** **+ key**, then press the key (Escape cancels).
* **Add a mouse button:** **+ mouse**.
* **Remove a binding:** click it.
* **Rename:** edit the action's or axis's name.

Several bindings per action are fine (Fire: the left mouse button or Return). The mapping is the project's, saved to
`Settings/Input.json` as key names (`"Space"`, `"Left Shift"`, `"Mouse Left"`) whenever it changes.

The defaults are:

| | |
| --- | --- |
| Actions | Jump (Space), Fire (left mouse, Return), Interact (F), Sprint (Left Shift), Blast (Q), Drop (B) |
| Axes | MoveX (D / A and the arrows), MoveY (W / S and the arrows) |

**Reset to defaults** brings them back. A name the mapping does not have is reported once in the console.

While the game plays, the keyboard belongs to it: the editor keeps only `E`, `Escape` and the F keys. Outside edit
mode the mouse is captured and `Input.MouseDelta` is how it moves.

**Hot reload.** Saving any file in `Scripts/` recompiles them all (a few hundred milliseconds), also while the game
plays: running scripts move to the new code with their fields (public and private) as they were, and get
`OnReload`. With compile errors (shown in the console and in the Inspector's Script section, with file and line),
the scripts keep running as they were. A script that throws is stopped (the exception and its stack, with line
numbers, go to the console) until its file is saved again. **Play > Reload Scripts** reloads by hand.

**How it works:**

* **Hosting:** the engine starts .NET 10 in its own process through the hosting API (`Scripting/ScriptHost`, which
  finds `hostfxr` as `nethost` would). It loads the scripting host `Scripting/FoxScripting`, built with the engine
  into `x64/<Configuration>/Scripting`.
* **Compiling:** the host compiles `Scripts/*.cs` with Roslyn, the C# compiler that comes with the .NET SDK. Each
  compile goes into an assembly of its own in a collectible load context, which the next reload replaces and unloads.
* **Calls:** both directions are tables of plain function pointers (`Scripting/ScriptApi.h`, mirrored by
  `Native.cs`), with no marshalling layer. Entities cross as 64 bit ids that stop being valid when the entity is
  destroyed.
* **Fields:** the Inspector reads each class's public fields (`float`, `int`, `bool`, `string`, `Vector3`) and their
  defaults from the host. A scene stores only the values that differ, as text.
* **Editing in an IDE:** `Scripts/Scripts.csproj` gives IntelliSense and errors as you type. The engine does not use
  it.
* **Requirements:** the .NET 10 SDK to build. Without a .NET runtime the engine runs with scripts off, and says so in
  the console and the Inspector.

**Limits:**

* A new mesh (`Spawn`, `Duplicate`, adding a Mesh) makes the scene rebuild once, a short pause. For things that
  appear often (shots, pickups), keep hidden ones ready and show them (`Active`, `Mesh.Visible`).
* Script fields cannot refer to entities directly; use names and `Entity.Find`.

### Physics

Rigid bodies with [Jolt Physics](https://github.com/jrouwe/JoltPhysics) (v5.3.0, MIT, vendored in
`ThirdParty/JoltPhysics`; `Scene/Physics` keeps its headers to itself). Give an entity a **Physics** component
(Inspector > Add Component, or **Physics > Add Body to Selection**), or make one from **Create > Physics** (a ball, a
metre box), then **Play** (`F5`, see *Play mode*). Bodies fall, collide and come to rest; their entities' transforms follow,
and everything rendered with them moves as the gizmos would move it. `F5` again stops, and every entity goes back
where it was. Saving while it plays saves the scene as it was before; loading a scene stops it.

| Motion | |
| --- | --- |
| Dynamic | Falls, collides, floats. Moving it with the gizmo while it plays puts it there and throws it with the speed it was moved at |
| Kinematic | Follows its entity's transform (the gizmo, a parent) and pushes dynamic bodies out of the way |
| Static | Stays where it was when physics started |

| Shape | |
| --- | --- |
| Auto | A sphere mesh's sphere; a model's convex hull (moving) or its triangles (static); a tree's trunk (a capsule as thick as its middle); otherwise the box |
| Box, Sphere, Capsule | Their sizes (and a centre offset) in the entity's space: its world scale scales them |
| Convex hull | Around the model's vertices (at most 256 corners), its parts where its glTF nodes put them (as rendered); kept while the scene lives, so playing again is quick |
| Triangle mesh | The model's triangles (without displacement, placed by its glTF nodes): exact, for static and kinematic bodies only (it has no inside) |

**Collision layers.** Every body is on one of 16 layers (**Layer** in its Physics component). The Environment window's
**Collision layers** names them (Default, Ground, Player, Enemy, Projectile, Trigger, Debris to start with; the
ground plane or terrain is on Ground) and has a matrix of which layers collide with which. The matrix applies at
once, also while playing. Bodies that do not move never collide with each other. Scripts can move a body to
another layer while playing (`Entity.Physics.Layer`).

**Triggers.** A body with **Trigger** on collides with nothing and notices what enters and leaves it (a Jolt
sensor; a static trigger notices static and kinematic bodies too). Queries pass through triggers unless asked
(`hitTriggers`).

**Events.** Scripts on a body's entity get:

* `OnCollisionEnter(Collision)` and `OnCollisionExit(Collision)`. `Collision.Other` is the other entity (None for the
  ground); `Point`; `Normal` points away from the other; `Speed` is how fast they closed in.
* `OnTriggerEnter(Entity)` and `OnTriggerExit(Entity)`, both for the trigger and for what entered it.

The events are collected while the physics steps run (on Jolt's threads) and handed to the scripts at the end of
the physics update. A pair touching in several places (a mesh's triangles) begins and ends once. Bodies falling
asleep while touching do not end their contact, and waking does not begin it again.

**Queries** (`Fox.Physics`, while playing; the ground only otherwise). Each takes a layer mask (`Layers.Mask("Ground",
"Enemy")`), an entity to ignore (usually yourself) and whether triggers count:

| Query | |
| --- | --- |
| `Raycast` / `RaycastAll` | The first body along a ray / every one, nearest first (one hit per body): entity, point, normal, distance |
| `SphereCast`, `BoxCast`, `CapsuleCast` | The shape swept along a direction: the first thing it touches |
| `OverlapSphere`, `OverlapBox`, `OverlapCapsule` | The entities whose bodies the shape touches |

`Scenes/scripting-demo.json` shows them: a goal zone (a trigger with `GoalZone`) glows while the player is in it.
`PlayerBall` logs hard landings (collision events), checks the ground with a raycast, and blasts nearby bodies away
with Q (an overlap).

The ground is the terrain (a 512 x 512 height field resampled from it) or the ground plane. Mass comes from
**Density** and the shape's volume unless **Mass** is set; friction, bounciness and damping are per body;
**Continuous collision** sweeps fast, small bodies so they do not pass through others. Steps are a fixed 1/60 s
(at most four a frame), on Jolt's job system threads.

**Water**: a dynamic body that **Floats** is lifted by the water it displaces (the plane of the surface under it,
tilted with the waves), slowed by **Water drag** and carried by the flow. Its buoyancy is water's density over its
own: lighter than 1000 kg/m^3 floats, heavier sinks. The surface, its tilt and the flow come from the simulation,
read back every frame (a few frames late). Hitting the water makes a splash as deep as the impact is hard (with its
foam and spray), and a body moving through the surface leaves a wake. The water does not flow around bodies: they
stir it only through these splashes.

### Navigation

Agents walk from where they are to a target over the ground (the terrain or the ground plane), on paths found with A*.
Give an entity a **Nav Agent** component (Inspector > Add Component, or **Physics > Add Nav Agent to Selection**), or
make one from **Create > Navigation > Agent** (a ball with a nose, facing +z). Select it and **Shift + click** the
ground: it plans its way there at once and the way shows in the view (cyan; orange when the target cannot be reached
and it goes as near as it can). **Play** (`F5`, with physics) walks it: it turns towards each turn of the path as
fast as its turn rate lets it, slowing while it turns, keeps the height above the ground it started at, and faces
where it walks (or tilts with the slope too). Shift + click while playing sends it somewhere else from where it is;
stopping puts it back.

The ground is a grid of square cells (**Grid cell**, 0.5 m; at most 1024 a side) around the way (30 m or half its
length beyond its ends, within the terrain if there is one). A cell is blocked by:

* meshes standing into the space it walks through: higher above the ground than its **Step** and lower than its
  **Height** (it walks under a high bridge or a roof's eaves, over a low kerb). Models block by their triangles
  (without displacement, placed by their glTF nodes as they are rendered): every triangle blocks the cells it covers
  from above where it is at a height in the way, so walls block and open doors do not. Spheres block by their sphere,
  trees by their trunk, physics bodies where they are when it plans. Other agents do not block (they move).
* water deeper than it **Wades to** (the simulated surface where there is one).

Blocked cells grow by the agent's **Radius**. A step to one of the eight neighbours climbs no steeper than the
**Steepest slope**, or no higher than a **Step**; steps cost their length along the ground, more uphill. The cells
A* finds are then straightened: the path goes straight wherever the ground between two of its points is walkable,
so it turns only at corners. An agent standing in something (or a target in something) starts from (or aims at) the
nearest open cell. A 73 m plan on the terrain scene (184 x 285 cells, two cabins' 557 000 triangles) takes about
50 ms; the default scene's plans take a few ms.

In edit mode an agent plans again when it is moved, once moved meshes have stood still a moment, after a terrain
stroke, and with **Plan again**. While playing it plans again (at most once a second) when something other than an
agent moved: a body falling or rolling into its way. The selected agent shows its blocked cells within 15 m (red dots); **Physics >
Show Agent Paths** hides the overlay. An agent with a dynamic Physics body moves it as a kinematic one, pushing what
is in its way. Agents do not avoid each other.

The code: `Scene/Navigation` (the grid, A* and straightening), `MainRayTracingSceneNavigation.cpp` (the grid from the
scene, and walking).

### Leaves, alpha testing and opacity micromaps

The tree's thinner branches (level 2 and up) carry leaf cards: two along each branch segment, and one at each tip.
About 800 cards in all, about 130 000 triangles. Each card shows `leaf-albedo.png`, an oak twig, and is cut out by
the texture's alpha. Each card is weighted to the bone of the branch it grows from, so it sways with it. The cards are
subdivided 8 × 10, droop towards their tip, and are displaced by the leaf height map. They use every leaf texture:

| Texture | Use |
| --- | --- |
| `leaf-albedo.png` | Base colour; its alpha cuts the card out (alpha cutoff 0.5) and builds the opacity micromaps |
| `leaf_normal.png` | Normal map, DirectX convention (green down) |
| `leaf-roughness.png`, `leaf-metallic.png`, `leaf_ambient_occlusion.png` | Packed into one texture: R occlusion, G roughness, B metallic |
| `leaf_displacement.png` | Card relief; stored inverted relative to the normal map, so the height used is 1 − value |

The normal map convention and the inverted displacement were found by comparing the slopes of the height map with the
normal map, in the data rather than by eye.

**Alpha testing.** A material with `alphaCutoff > 0` is alpha tested. Its geometry is built without the opaque flag,
so rays run the any-hit shader (`Shaders/any_hit.slang`), which discards hits where alpha is below the cutoff. Shadow
ray queries test such candidates themselves. Leaves therefore cast dappled shadows.

**Opacity micromaps.** Calling the any-hit shader for every leaf candidate is slow, so each alpha tested triangle also
gets a 4-state opacity micromap (`Graphics/Vulkan/Raytracing/OpacityMicromap`):

* **Subdivision:** the triangle is split into micro-triangles about 2 texels across (4^7 = 16 384 per triangle on the
  cards).
* **Classification:** each micro-triangle is sampled densely against the alpha. It is marked opaque or transparent if
  all samples agree, and *unknown* if it straddles a leaf's outline.
* **Effect:** the hardware accepts opaque and skips transparent micro-triangles by itself. Only unknown ones run the
  any-hit shader.

Micro-triangle order follows the reference function in the Vulkan specification. The builder checks that every
micro-triangle index is used exactly once.

Triangles with identical texture coordinates share a micromap entry, and triangles that are entirely opaque or
transparent use a special index with no data. All 130 560 leaf triangles therefore need only 100 entries (1.6 MB).
Micromaps live in barycentric space, so they stay valid while the branches bend and the acceleration structure is
refitted.

On an RTX 5090, with leaves filling part of the view, the micromaps take about 10–15% off the trace pass. Set
`FOX_NO_OPACITY_MICROMAPS=1` to compare (alpha testing then all happens in the any-hit shader; the image is the same).
GPUs without the extension use that path automatically. Any glTF material can use the same machinery: set its
`alphaCutoff`, and give the scene the texture's alpha with `RayTracingScene::SetAlphaMask`.

Not modelled yet: light passing through leaves (translucency), so backlit leaves look darker than real ones.

### Editing the scene

Press `E` for edit mode: the mouse pointer appears and the window title shows the gizmo mode and the selection.

Edit mode has a menu bar (File, Edit, Create, Window, Render, Camera, Help), a status bar at the bottom and, at
first, only the **Hierarchy**. The rest open from the **Window** menu; the **Inspector** also opens when something is
selected. Windows go where they belong around the view: Hierarchy on the left, Inspector on the right, the tool
windows (Environment, Trees, Terrain, Camera Paths) beside it, the Content Browser along the bottom.
**Window > Reset Layout** puts them back, and a new window size (`T` fullscreen) does too.

| Input | Action |
| --- | --- |
| `F1` | **Find a command**: type part of any menu command's name (`light`, `save`, `trees`, `denoiser`...), Up / Down to choose, Enter to run. The field at the right of the menu bar does the same with the mouse |
| `Ctrl+Space` | **Content Browser** on / off: `Models/`, `Scenes/` and `Textures/`. Double-click a model to add it in front of the camera, or drag it onto an entity in the Hierarchy to add it as that entity's child; double-click a scene to open it |
| `Ctrl+S` | Save the scene |
| `Ctrl+D` | Duplicate the selected entity |
| `M` | Mesh paint brush on / off (see *Instanced trees and mesh painting*) |

**Help > Controls** lists every key. Both `F1` and `Ctrl+Space` switch on edit mode when it is off.

| Input | Action |
| --- | --- |
| Left click | Select the entity under the pointer (the ground cannot be selected) |
| `Z` / `X` / `C` | Move / rotate / scale gizmo |
| Drag an arrow | Move along that world axis; the center square moves in the view plane |
| Drag a ring | Rotate about that world axis |
| Drag an axis | Scale along that axis of the object; the center square scales uniformly |
| `Ctrl` while dragging | Snap to 0.25 m, 15° or 0.1x steps |
| Right mouse + drag | Look around (`W` `A` `S` `D` `R` `F` still move) |
| `Delete` | Delete the selected entity and everything under it |
| `K` | Add a camera path key from the view |
| `B` | Terrain brush on / off (left mouse on the terrain sculpts or paints, `Ctrl` inverts, `[` `]` radius) |
| `E` | Leave edit mode |

Each entity rotates and scales about its origin, in world space; a child's local transform is set to match.
`SceneGraph::SetLocalTransform` / `SetWorldTransform` move entities from code too.

How it works: the path tracer reports the object seen at the clicked pixel (`gPickResult` in `raygen.slang`), read back
once that frame finishes. When an object moves, the top level acceleration structure is rebuilt that frame
(microseconds: one instance per object), its lights follow it, and every instance's motion since the previous frame
goes to the shaders, which add it to the motion vectors of directly visible surfaces. The gizmos are 2D triangles
drawn over the tone mapped image (`Editor/TransformGizmo`, `Graphics/Vulkan/OverlayRenderer`).

### Scene graph and components

The scene's content is a tree of entities (`Scene/SceneGraph`) with components (`Scene/ECS`): a small
entity-component system whose registry keeps each component type in a packed array (a sparse set).

| Component | What it does |
| --- | --- |
| Transform | Position, rotation and scale relative to the parent (the world for a root). Every new entity has one |
| Mesh | The entity is rendered: a glTF model, a sphere or a generated tree. **Rendered** off hides it without a rebuild |
| Light | The entity gives light: directional (the sun), point or spot. It shines along the entity's -Z axis; point and spot lights are where the entity is. **On** off darkens it without a rebuild |
| Water | A body of water, simulated and refracting (see *Water*) |
| Physics | A rigid body, simulated while physics plays (see *Physics*) |
| Nav Agent | It walks over the ground to a target on a path found with A* (see *Navigation*) |
| Volume | A puff or box of smoke that light scatters in, optionally glowing (see *Volumetrics*) |
| Camera | The game looks through it while it plays (see *Play mode*) |
| Script | C# scripts that run on it while the game plays (see *Scripting*) |
| Prefab | It is an instance of a prefab: what is under it comes from the prefab, with its own overrides (see *Prefabs*) |
| Sound | It plays a sound from where it is, or background music (see *Audio*) |

Every scene starts with a **Sun** entity (a directional light): turn it to move the sun, or use the light's
**Direction** and **Elevation**. Its **Size** (apparent radius in degrees) softens the shadows; a point or spot
light's **Bulb radius** does the same. Point and spot intensity is the irradiance 1 m away, falling off with the
square of the distance; a spot is full inside its inner angle and fades out towards its outer angle. Lights have no
geometry: they are not seen directly or in mirrors, only by what they light. Adding or removing a light rebuilds the
scene once (room in the light buffer); anything else about a light (moving it, colour, type) needs no rebuild.

Every entity also has a name and an **active** flag; an inactive entity hides everything under it. A child moves,
turns and scales with its parent. In edit mode:

| Window | What it does |
| --- | --- |
| **Hierarchy** | Every entity as a tree. Click to select (the same selection as clicking in the view). Drag onto an entity to make it a child, onto the top or bottom edge of one to place it before or after, onto the empty space below to make it a root. Right-click: create an empty child, duplicate (`Ctrl+D`), unparent, active, delete. **+ Create**: an empty entity, a sphere, a tree or a light |
| **Inspector** | The selected entity: active, name, and each component in a section (transform with Euler angles in degrees, the mesh's type, file, sphere material). **Add Component** adds one; a section's close button (or right-click on Transform) removes it |

Adding a component type (physics, audio, ...) takes its struct in `Scene/ECS/Components.h`, an inspector entry in
`COMPONENT_EDITORS` (`Editor/SceneGraphEditor.cpp`), a save entry in `COMPONENT_SERIALIZERS` (`Scene/SceneFile.cpp`)
and a system that acts on it, stepped from `MainRayTracingScene::Update` like the mesh sync.

How it works: rebuilding the scene places every entity with a mesh as a ray tracing scene object, at its world
transform. Each frame the graph's world transforms are updated and objects whose entity moved (or was shown or
hidden) follow, through the same path as the gizmos. Adding or removing a mesh, or changing its type, file or sphere,
rebuilds the scene; trees only rebuild the trees.

### Scene files

Nothing is built into the code: the scene starts from `Scenes/default.json` (relative to the working directory), and
without that file it starts empty, with only the ground and the sun. In edit mode:

| Where | What it does |
| --- | --- |
| **File** menu | New Scene, Open Scene (the Content Browser's `Scenes/`), Save Scene (`Ctrl+S`, with the current view), Save Scene As. Save to `Scenes/default.json` to change what the renderer starts with. The scene's file shows in the status bar |
| **Create** menu | Empty entities, a model (the Content Browser's `Models/`, optionally with displacement from its normal maps), spheres with a glass, frosted glass, chrome, gold or plastic material, a tree, and lights. New entities stand on the ground 12 m in front of the camera |
| **Environment** window | Ground (height, colour, roughness). The sun is an entity with a Light component |

The format (`Scene/SceneFile.cpp`), every value optional, defaults filled in for what a file leaves out:

```json
{
  "version": 4,
  "camera": { "position": [29, -2.6, 17], "target": [4, -0.6, -3] },
  "environment": { "ground": true, "groundHeight": -3.73 },
  "entities": [
    { "name": "Cabin", "active": true, "parent": -1,
      "components": {
        "transform": { "position": [20, 0, 0], "rotation": [0, 90, 0], "scale": [1, 1, 1] },
        "mesh": { "type": "model", "file": "Models/cabin.glb", "displacement": true } } },
    { "name": "Glass ball", "parent": 0,
      "components": {
        "transform": { "position": [1, -2.53, 10] },
        "mesh": { "type": "sphere", "radius": 1.2,
                  "material": { "color": [1, 1, 1], "roughness": 0, "transmission": 1, "ior": 1.5,
                                "attenuationColor": [0.8, 0.94, 0.9], "attenuationDistance": 2 } } } },
    { "name": "Tree 1", "components": { "transform": { "position": [14, -3.73, 12] },
                                         "mesh": { "type": "tree", "asset": 0 } } },
    { "name": "Sun", "components": { "transform": { "rotation": [-13, 61, 0] },
                                     "light": { "type": "directional", "color": [1, 0.58, 0.3], "intensity": 18, "angularRadius": 1.2 } } },
    { "name": "Lamp", "components": { "transform": { "position": [18, 1, 4] },
                                      "light": { "type": "spot", "intensity": 50, "radius": 0.05, "innerConeAngle": 20, "outerConeAngle": 30 } } }
  ],
  "treeAssets": [ { "shape": {}, "look": {}, "wind": {} } ]
}
```

Entities are listed parents first; `parent` is the index of the parent in the list (`-1` or none: a root), and a
child's transform is relative to its parent. An entity without `"transform"` has no Transform component. Version 1
files (`"models"`, `"spheres"` and `"trees"` lists) still load, as root entities, and files before version 3 have
their environment's sun (`"sun"`, `"sunDirection"`, ...) turned into a Sun entity. Rotations are Euler angles in
degrees. A tree's `shape`, `look` and `wind` hold everything the Trees window edits
(saved in full, so a file shows every parameter). Adding, removing or loading rebuilds the scene's acceleration
structures; models already loaded once are kept in memory, so this takes about half a second with two displaced
cabins (the first load of a displaced model takes a few seconds).

### Terrain

In place of the flat ground plane the scene can have a terrain: **Terrain instead of the ground plane** in the
**Terrain** window (edit mode). It starts flat at the ground height, all grass, 400 m across with a height every
0.78 m (513 x 513) and a 1024 x 1024 blend map; **Size** changes those (the existing shape and paint are resampled).
`Scenes/terrain.json` is a small example.

**Brush** (left mouse on the terrain while *Brush on* is ticked, `B`): a round dab with a smooth edge, applied for as
long as the button is held. *Raise* / *Lower* (meters per second at the center, `Ctrl` swaps them), *Smooth*,
*Flatten* (to the height where the stroke started) and *Paint* (one of the four layers). *Hardness* keeps full
strength further out before the soft edge; `[` and `]` change the radius. Clicks off the terrain still select
objects.

**Layers**: four ground types, one per channel of the blend map. By default the sets in `Textures/terrain` (grass,
rocky, badland, bumpy). Each layer has a texture set (any folder with `albedo.png`, `normal.png`, `roughness.png`,
`metallic.png`, `ao.png`, `displacement.png`, or single files), a tiling in meters, a tint, a roughness factor and a
normal strength; changes apply at once, without rebuilding the scene.

How it looks right: the layer textures are not mapped with the mesh's texture coordinates but projected from the
position along the three axes (triplanar mapping, weighted by the surface normal), so steep slopes and cliffs keep
the texture's proportions instead of stretching it; normal maps are projected the same way ("whiteout" blend). Where
layers mix, each layer's height map decides which one shows (*blend softness*): rocks poke out between grass instead
of the two fading into each other. All of it is in `Shaders/terrain.slang`.

How editing stays fast: the terrain is one mesh whose vertices are rewritten in place (only the rows a dab touched),
its acceleration structure refitted every frame of a stroke and built anew when the button is released; only the
changed texels of the blend map are uploaded (`RayTracingScene::AddEditableSurface`, `UpdateSurfaceVertices`,
`UpdateTextureRegion`, recorded with the frame in `RecordEdits`).

Saved with the scene: the settings and layers in the scene file, the heights in `<scene>.terrain-heights.bin` (32 bit
floats) and the blend map in `<scene>.terrain-blend.png` (RGBA = the four layers' weights; it can be painted in any
image editor too).

```json
"environment": { "terrain": true, ... },
"terrain": { "size": 400, "resolution": 513, "blendResolution": 1024, "position": [0, -3.73, 0], "blendSoftness": 0.15,
             "layers": [ { "name": "Grass", "albedo": "Textures/terrain/grasss/albedo.png", "tiling": 2.5, ... }, ... ],
             "heights": "Scenes/terrain.terrain-heights.bin", "blend": "Scenes/terrain.terrain-blend.png" }
```

### Camera paths

A scene can hold camera paths: keys (where the camera is and what it looks at) joined by a centripetal Catmull-Rom
spline, which passes through every key without loops or overshoot where keys are unevenly spaced. The camera flies
the active path at a constant speed whatever the spacing of the keys (an arc length table maps distance to the
spline), speeding up from rest and slowing down to it over the *ease* time at the ends. A *loop* path joins its last
key to the first and goes round until stopped; *look along the path* turns the camera where the path goes instead
of where the keys look. The default scene has one, *Around the cabin*: press `P` to fly it.

| Input | Action |
| --- | --- |
| `P` | Fly the active path, or stop (from the start again once it has ended) |
| `K` (edit mode) | Add a key where the camera is and where it looks, after the selected key (makes a path if there is none) |

The **Camera** window (edit mode) sets the free flight speed and edits paths: new, remove, name, speed, ease, loop,
look along the path, play / stop, a time slider to scrub through the flight, and the keys (select, double click or
**Go to** to look from one, **Set to view**, reorder, remove, or type positions). The active path is drawn in the
view, with each key's number and where it looks. While a path plays, the mouse and `W` `A` `S` `D` `R` `F` do not move
the camera and the window title shows the time. Paths are saved in the scene file:

```json
"cameraPaths": [
  { "name": "Around the cabin", "speed": 6, "ease": 1.5, "loop": true, "lookAlongPath": false,
    "keys": [ { "position": [29, -2.6, 17], "target": [4, -0.6, -3] }, { "position": [12, -1.5, 30], "target": [12, -1.5, 4] } ] }
],
"activeCameraPath": 0
```

The code: `Scene/CameraManager` (paths, playback, free flight), `Editor/CameraEditor` (the window and the drawn path).

### Adding a denoiser

Derive from `Denoiser` (`Graphics/Vulkan/Raytracing/Denoiser.h`), implement `Resize` / `Denoise` / `GetOutputView`, and register the class in
`CreateDenoisers()` (`Denoiser.cpp`). The path tracer hands every denoiser the same G-buffer (`GBuffer.h`): noisy radiance, diffuse and
specular albedo, normal, roughness, depth, motion vectors and specular hit distance. `ComputePass` wraps a full-screen compute shader.

## 🎮 Controls

| Input | Action |
| --- | --- |
| Mouse | Look around |
| `W` `A` `S` `D` | Move |
| `R` / `F` | Move up / down |
| `N` | Next denoiser (DLSS Ray Reconstruction → SVGF → Accumulation) |
| `1` / `2` | Exposure down / up (1/3 stop; saved with the scene) |
| `3` / `4` | Fewer / more paths per pixel |
| `9` / `0` | Decrease / increase environment map intensity |
| `U` | Next upscaling mode (Native → Quality → Balanced → Performance → Ultra Performance). Chosen from the display size until pressed |
| `V` | Toggle vsync |
| `T` | Toggle fullscreen (exclusive, at the desktop resolution) |
| `P` | Fly the active camera path, or stop (see *Camera paths*) |
| `F5` | Play, or stop (the scene goes back as it was; see *Play mode*) |
| `F6` | Pause or resume game time (playing) |
| `F7` | Step one frame (playing; pauses) |
| `F8` | Free camera, or back to the game's camera (playing) |
| `Shift` + click (edit mode) | Send the selected agent there (see *Navigation*) |
| `Alt` + drag | Pull cloth (edit mode and play mode; a hard pull tears a tearable one) |
| `Alt` + `Shift` + drag | Cut cloth along the way the mouse goes |
| `E` | Edit mode: select, move, rotate and scale objects, the Scene, Trees and Camera windows (see *Editing the scene*, *Scene files*, *Camera paths*, *Tree editor*) |
| `Esc` | Quit |

The window title shows the active denoiser, the upscaling mode and render resolution, the paths per pixel, the frame rate
and the GPU time of each pass. Upscaling modes other than Native are DLSS's; the other denoisers are upscaled bilinearly.

---

## 🛠️ Road Map / Future Developments

Below is the active development checklist for the renderer.

### 💡 Rendering & Lighting

- [x] Multiple Importance Sampling (MIS) lighting
- [x] Refraction and Fresnel effects
- [ ] Caustics (light focused through glass)
- [x] Recursive BRDF evaluation / ray depth handling
- [x] Normal mapping
- [x] Displacement mapping
- [x] Denoiser integration (SVGF & DLSS Ray Reconstruction)

### ⛰️ Procedural & Special Effects
- [x] Terrain: sculpted and painted in the editor (triplanar, height blended layers)
- [ ] Procedural terrain generation
- [x] Procedural tree generation
- [ ] Particle systems and GPU-driven effects

### 🏗️ Scene Graph & Performance
- [ ] Acceleration structure (BLAS/TLAS) refitting
- [ ] Full hierarchical scene graph
- [x] Instance buffer updates for dynamic object movement (editor gizmos)
- [x] Scene configuration files using JSON (`Scenes/*.json`, saved and loaded from the File menu)

### 🏃 Animation & Physics
- [x] GPU skinning (procedural tree in the wind, angular springs)
- [ ] Skeletal animation support
- [x] Integration of a physics engine (Jolt Physics: rigid bodies, terrain, buoyancy in the simulated water)
- [x] Cloth: XPBD with self collision, wind, collisions, two way coupling with rigid bodies, tearing and cutting

### 🐛 Bug Fixes & Refinement
- [ ] Improved glTF binary format compliance (roughness, metallic parameter checks)

---