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
* **Lighting** Importance-sampled HDR environment map, emissive quads as area lights and a sun with soft shadows.
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

| Input | Action |
| --- | --- |
| Left click | Select the object under the pointer (the ground cannot be selected) |
| `Z` / `X` / `C` | Move / rotate / scale gizmo |
| Drag an arrow | Move along that world axis; the center square moves in the view plane |
| Drag a ring | Rotate about that world axis |
| Drag an axis | Scale along that axis of the object; the center square scales uniformly |
| `Ctrl` while dragging | Snap to 0.25 m, 15° or 0.1x steps |
| Right mouse + drag | Look around (`W` `A` `S` `D` `R` `F` still move) |
| `Delete` | Remove the selected object |
| `E` | Leave edit mode |

Each object rotates and scales about its origin. `RayTracingScene::SetObjectTransform` moves objects from code too.

How it works: the path tracer reports the object seen at the clicked pixel (`gPickResult` in `raygen.slang`), read back
once that frame finishes. When an object moves, the top level acceleration structure is rebuilt that frame
(microseconds: one instance per object), its lights follow it, and every instance's motion since the previous frame
goes to the shaders, which add it to the motion vectors of directly visible surfaces. The gizmos are 2D triangles
drawn over the tone mapped image (`Editor/TransformGizmo`, `Graphics/Vulkan/OverlayRenderer`).

### Scene files

Nothing is built into the code: the scene starts from `Scenes/default.json` (relative to the working directory), and
without that file it starts empty, with only the ground and the sun. In edit mode the **Scene** window:

| Part | What it does |
| --- | --- |
| Path, **Save** / **Load** / **New** | Save the scene (with the current view) to the path, load a scene file, or start an empty scene. Save to `Scenes/default.json` to change what the renderer starts with |
| **Add** | Add any glTF model from `Models/` (`.glb` / `.gltf`, optionally with displacement from its normal maps), or a sphere with a glass, frosted glass, chrome, gold or plastic material. New objects stand on the ground 12 m in front of the camera. Trees are added in the **Trees** window |
| **Objects** | Every model, sphere and tree; click one to select it for the gizmo. **Remove selected** (or `Delete`) removes it |
| **Environment** | Ground (height, colour, roughness) and sun (direction, elevation, colour, strength, size) |

The format (`Scene/SceneFile.cpp`), every value optional, defaults filled in for what a file leaves out:

```json
{
  "version": 1,
  "camera": { "position": [29, -2.6, 17], "target": [4, -0.6, -3] },
  "environment": { "ground": true, "groundHeight": -3.73, "sun": true, "sunDirection": [0.876, 0.22, 0.479], "sunIntensity": 18 },
  "models": [
    { "name": "Cabin", "file": "Models/cabin.glb", "displacement": true,
      "transform": { "position": [20, 0, 0], "rotation": [0, 90, 0], "scale": [1, 1, 1] } }
  ],
  "spheres": [
    { "name": "Glass ball", "radius": 1.2, "transform": { "position": [21, -2.53, 10] },
      "material": { "color": [1, 1, 1], "roughness": 0, "transmission": 1, "ior": 1.5,
                    "attenuationColor": [0.8, 0.94, 0.9], "attenuationDistance": 2 } }
  ],
  "trees": [ { "name": "Tree 1", "transform": { "position": [14, -3.73, 12] }, "shape": {}, "look": {}, "wind": {} } ]
}
```

Rotations are Euler angles in degrees. A tree's `shape`, `look` and `wind` hold everything the Trees window edits
(saved in full, so a file shows every parameter). Adding, removing or loading rebuilds the scene's acceleration
structures; models already loaded once are kept in memory, so this takes about half a second with two displaced
cabins (the first load of a displaced model takes a few seconds).

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
| `E` | Edit mode: select, move, rotate and scale objects, the Scene and Trees windows (see *Editing the scene*, *Scene files*, *Tree editor*) |
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
- [ ] Procedural terrain generation
- [x] Procedural tree generation
- [ ] Particle systems and GPU-driven effects

### 🏗️ Scene Graph & Performance
- [ ] Acceleration structure (BLAS/TLAS) refitting
- [ ] Full hierarchical scene graph
- [x] Instance buffer updates for dynamic object movement (editor gizmos)
- [x] Scene configuration files using JSON (`Scenes/*.json`, saved and loaded in the Scene window)

### 🏃 Animation & Physics
- [x] GPU skinning (procedural tree in the wind, angular springs)
- [ ] Skeletal animation support
- [ ] Integration of a physics engine (e.g., Box2D/3D physics library)

### 🐛 Bug Fixes & Refinement
- [ ] Improved glTF binary format compliance (roughness, metallic parameter checks)

---