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