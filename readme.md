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
* **Displacement Mapping** Surfaces are tessellated and displaced when a model loads, so the relief is real geometry
  (silhouettes, shadows, reflections). Height maps can be assigned per material; models without them (glTF has no slot
  for displacement) get heights reconstructed from their normal maps.
* **Denoising** Pluggable denoisers, switchable at runtime:
  * **DLSS Ray Reconstruction** (NVIDIA, optional): neural denoising + anti-aliasing with the newest transformer model.
  * **SVGF** (Spatiotemporal Variance-Guided Filtering): temporal reprojection + variance-guided a-trous wavelet filter, in compute shaders.
  * **Accumulation**: plain averaging while the camera is still, as the ground-truth reference.
* **HDR Pipeline** Floating point rendering and an AgX tone mapping pass with exposure control.
* **Skinning & Wind** Skinned meshes are deformed on the GPU every frame (linear blend skinning in a compute shader,
  acceleration structures refitted). A procedural tree is built out of cylinders with a bone per cylinder, and every
  joint is an angular spring pushed by gusty wind.
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

### Performance

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

* Wind torque: quadratic drag on every branch segment, summed over everything the joint carries, about the joint.
* Inertia: of that subtree about the joint. The stiffness `k` follows from a natural frequency that rises from
  0.45 Hz for the trunk to a few Hz for twigs. Damping is a fraction of critical.
* Gusts: roll across the tree with the wind, and branches flutter sideways.

Settings such as wind speed, gust strength, damping and response are in `WindSkeleton::Settings`; tree shape in
`TreeSettings`.

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

### Physics

Rigid bodies with [Jolt Physics](https://github.com/jrouwe/JoltPhysics) (v5.3.0, MIT, vendored in
`ThirdParty/JoltPhysics`; `Scene/Physics` keeps its headers to itself). Give an entity a **Physics** component
(Inspector > Add Component, or **Physics > Add Body to Selection**), or make one from **Create > Physics** (a ball, a
metre box), then **Physics > Play** (`F5`). Bodies fall, collide and come to rest; their entities' transforms follow,
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
| Convex hull | Around the model's vertices (at most 256 corners); kept while the scene lives, so playing again is quick |
| Triangle mesh | The model's triangles (without displacement): exact, for static and kinematic bodies only (it has no inside) |

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
| `1` / `2` | Exposure down / up (1/3 stop) |
| `3` / `4` | Fewer / more paths per pixel |
| `9` / `0` | Decrease / increase environment map intensity |
| `U` | Next upscaling mode (Native → Quality → Balanced → Performance → Ultra Performance). Chosen from the display size until pressed |
| `V` | Toggle vsync |
| `T` | Toggle fullscreen (exclusive, at the desktop resolution) |
| `P` | Fly the active camera path, or stop (see *Camera paths*) |
| `F5` | Play physics and agents, or stop (everything goes back where it was; see *Physics*) |
| `Shift` + click (edit mode) | Send the selected agent there (see *Navigation*) |
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

### 🐛 Bug Fixes & Refinement
- [ ] Improved glTF binary format compliance (roughness, metallic parameter checks)

---