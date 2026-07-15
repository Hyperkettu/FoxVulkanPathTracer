\#\[Fox Vulkan Path Tracing Renderer](image.png)



\# Fox Vulkan Path Tracing Renderer



Fox Vulkan Path Tracing Renderer is implemented by using the low-level Vulkan API and C++.



!\[Fox Vulkan Path Tracing Renderer Screenshot](FoxPathTracer/Textures/box.png)



The renderer can load glTF 2.0 files in both binary (\\`.glb\\`) and JSON text (\\`.gltf\\`) formats. It utilizes the Cook-Torrance BRDF (Bidirectional Reflective Distribution Function) to accurately calculate the portion of reflected and refracted light on a surface.



\## Features \& Implementation Details

\- \*\*Low-Level Vulkan API:\*\* Built from the ground up using C++ and modern Vulkan APIs.

\- \*\*glTF 2.0 Support:\*\* Parsing and rendering of binary and text-based glTF scenes.

\- \*\*Physically Based Rendering (PBR):\*\* Implements Cook-Torrance microfacet BRDF for realistic material shading.



\## TODO List



\- \[ ] Multiple Importance Sampling (MIS) lighting

\- \[ ] Refraction and Fresnel effects

\- \[ ] Recursive BRDF evaluation / ray depth handling

\- \[ ] Procedural terrain generation

\- \[ ] Procedural tree generation

\- \[ ] Particle systems and GPU-driven effects

\- \[ ] Acceleration structure (BLAS/TLAS) refitting

\- \[ ] Full hierarchical scene graph

\- \[ ] Skeletal animation support

\- \[ ] Integration of a physics engine (e.g., Box2D for 2D, or a 3D physics library)

\- \[ ] Instance buffer updates for dynamic object movement

\- \[ ] Improved glTF binary format compliance (roughness, metallic parameter checks)

\- \[ ] Scene configuration files using JSON (Fox Scene format)

\- \[ ] Denoiser integration (SVGF and DLSS 4.5/Ray Reconstruction)

\- \[ ] Normal mapping and displacement mapping

