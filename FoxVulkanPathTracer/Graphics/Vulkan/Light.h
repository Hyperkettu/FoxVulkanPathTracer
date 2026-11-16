#pragma once 

#include <glm/glm.hpp>

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            struct Light {
                alignas(16) glm::vec3 position;  // Center of the quad
                alignas(16) glm::vec3 normal;    // Quad: surface normal. Directional: direction towards the light
                alignas(16) glm::vec3 tangentX;  // Left-to-right vector spanning half the width
                alignas(16) glm::vec3 tangentY;  // Back-to-front vector spanning half the height
                alignas(16) glm::vec3 color;     // Quad: emitted radiance. Directional: irradiance on a surface facing the light
                alignas(4)  float area;          // Quad: width * height. Directional: angular radius of the disc in radians
                alignas(4)  int type;            // Fox::Core::Loaders::GLTF::LightType
            };
        }
    }
}