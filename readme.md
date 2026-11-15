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
* **Physically Based Shading** Implements a Cook-Torrance BRDF to realistically compute diffuse, specular, reflection light paths based on surface roughness and metallicity.
* **Shadows** Implements ray query shadows. 
* **Progressive Accumulation** Frames are averaged into an HDR buffer while the camera is still, so the image converges over time.
* **HDR Pipeline** Radiance HDR environment maps, a floating point render target and an ACES tone mapping pass.

---

## 🔧 Building

**Requirements**

* Windows, Visual Studio 2022 (v143 toolset, C++20)
* [Vulkan SDK](https://vulkan.lunarg.com/) 1.4+ installed with the **SDL2** and **GLM** components. The project locates
  everything (headers, libraries and the `slangc` shader compiler) through the `VULKAN_SDK` environment variable that the installer sets.
* A GPU with hardware ray tracing (`VK_KHR_ray_tracing_pipeline`, `VK_KHR_ray_query`) and mesh shader (`VK_EXT_mesh_shader`) support.

**Steps**

1. Open `FoxVulkanPathTracer.sln` and select the `x64` platform (`Debug` or `Release`).
2. Build. The Slang shaders in `FoxVulkanPathTracer/Shaders` are compiled to SPIR-V as part of the build
   (`Shaders/compile_shaders_slang.bat` does the same by hand).
3. Run from Visual Studio. Assets are loaded relative to the project directory (`FoxVulkanPathTracer/`),
   which is the default debugger working directory. `SDL2.dll` is picked up from the Vulkan SDK's `Bin` folder on `PATH`.

`Debug` builds enable the Vulkan validation layers and print warnings and errors to the console.

## 🎮 Controls

| Input | Action |
| --- | --- |
| Mouse | Look around |
| `W` `A` `S` `D` | Move |
| `R` / `F` | Move up / down |
| `9` / `0` | Decrease / increase environment map intensity |
| `7` / `8` | Cycle the texture used as environment map (debug) |
| `T` | Toggle fullscreen |
| `Esc` | Quit |

---

## 🛠️ Road Map / Future Developments

Below is the active development checklist for the renderer.

### 💡 Rendering & Lighting

- [ ] Multiple Importance Sampling (MIS) lighting
- [ ] Refraction and Fresnel effects
- [ ] Recursive BRDF evaluation / ray depth handling
- [ ] Normal mapping and displacement mapping
- [ ] Denoiser integration (SVGF & DLSS 4.5/Ray Reconstruction)

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