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
  `KHR_materials_transmission`, `KHR_materials_specular`, `KHR_materials_emissive_strength`.
* **Lighting** Importance-sampled HDR environment map, emissive quads as area lights and a sun with soft shadows.
* **Displacement Mapping** Surfaces are tessellated and displaced when a model loads, so the relief is real geometry
  (silhouettes, shadows, reflections). Height maps can be assigned per material; models without them (glTF has no slot
  for displacement) get heights reconstructed from their normal maps.
* **Denoising** Pluggable denoisers, switchable at runtime:
  * **DLSS Ray Reconstruction** (NVIDIA, optional): neural denoising + anti-aliasing with the newest transformer model.
  * **SVGF** (Spatiotemporal Variance-Guided Filtering): temporal reprojection + variance-guided a-trous wavelet filter, in compute shaders.
  * **Accumulation**: plain averaging while the camera is still, as the ground-truth reference.
* **HDR Pipeline** Floating point rendering and an AgX tone mapping pass with exposure control.
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

**Optional: DLSS Ray Reconstruction**

The DLSS denoiser is compiled in automatically when NVIDIA's DLSS SDK is found in `ThirdParty/DLSS` next to the solution:

```bash
git clone --depth 1 --branch v310.9.16 https://github.com/NVIDIA/DLSS.git ThirdParty/DLSS
```

The SDK is not part of this repository (it is listed in `.gitignore`) and is covered by NVIDIA's own license (`ThirdParty/DLSS/LICENSE.txt`).
The build copies `nvngx_dlssd.dll` next to the executable. At runtime it needs an RTX GPU and a current driver;
if DLSS is unavailable the renderer says so on the console and starts with SVGF instead.

### Displacement mapping

Displacement is applied while a model loads (`Graphics/Geometry/Displacement.h`) and is configured per model, see
`MainRayTracingScene.cpp`. The glTF format has no displacement map slot, so the height source of each material is:

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
| `Esc` | Quit |

The window title shows the active denoiser, the upscaling mode and render resolution, the paths per pixel, the frame rate
and the GPU time of each pass. Upscaling modes other than Native are DLSS's; the other denoisers are upscaled bilinearly.

---

## 🛠️ Road Map / Future Developments

Below is the active development checklist for the renderer.

### 💡 Rendering & Lighting

- [x] Multiple Importance Sampling (MIS) lighting
- [ ] Refraction and Fresnel effects
- [x] Recursive BRDF evaluation / ray depth handling
- [x] Normal mapping
- [x] Displacement mapping
- [x] Denoiser integration (SVGF & DLSS Ray Reconstruction)

### ⛰️ Procedural & Special Effects
- [ ] Procedural terrain generation
- [ ] Procedural tree generation
- [ ] Particle systems and GPU-driven effects

### 🏗️ Scene Graph & Performance
- [ ] Acceleration structure (BLAS/TLAS) refitting
- [ ] Full hierarchical scene graph
- [ ] Instance buffer updates for dynamic object movement
- [ ] Scene configuration files using JSON (*Fox Scene format*)

### 🏃 Animation & Physics
- [ ] Skeletal animation support
- [ ] Integration of a physics engine (e.g., Box2D/3D physics library)

### 🐛 Bug Fixes & Refinement
- [ ] Improved glTF binary format compliance (roughness, metallic parameter checks)

---