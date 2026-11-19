#pragma once 

#include <glm/glm.hpp>

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            // Mirrors Light in Shaders/lighting.slang (128 bytes; each vec3 takes 16)
            struct Light {
                alignas(16) glm::vec3 position;  // Quad: center. Point / spot: where it is. Triangle: its first corner
                alignas(16) glm::vec3 normal;    // Quad, triangle: surface normal. Directional: direction towards the light. Spot: direction it shines
                alignas(16) glm::vec3 tangentX;  // Quad: left-to-right vector spanning half the width. Spot: x = cos(outer cone angle), y = cos(inner). Triangle: first edge
                alignas(16) glm::vec3 tangentY;  // Quad: back-to-front vector spanning half the height. Triangle: second edge
                alignas(16) glm::vec3 color;     // Quad, triangle: emitted radiance. Directional: irradiance on a surface facing the light. Point / spot: intensity (irradiance at 1 m)
                alignas(16) float area = 0.0f;   // Quad, triangle: surface area. Directional: angular radius of the disc in radians. Point / spot: bulb radius (m)
                int type = 0;                    // Fox::Core::Loaders::GLTF::LightType
                int material = -1;               // Triangle: its material (emission and emissive map are read from it)
                // How light sampling picks it (RayTracingScene::BuildLightSelection): its probability, and the running
                // sum of the probabilities up to and including it
                float selection = 0.0f;
                float cumulative = 0.0f;
                float u0 = 0.0f, v0 = 0.0f;      // Triangle: texture coordinates of its corners (the emissive map)
                float u1 = 0.0f, v1 = 0.0f;
                float u2 = 0.0f, v2 = 0.0f;
                float pad = 0.0f;
            };
            static_assert(sizeof(Light) == 128 && offsetof(Light, area) == 80, "Light layout must match lighting.slang");
        }
    }
}