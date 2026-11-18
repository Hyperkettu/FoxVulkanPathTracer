#pragma once 

#include <glm/glm.hpp>

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            struct Light {
                alignas(16) glm::vec3 position;  // Quad: center. Point / spot: where it is
                alignas(16) glm::vec3 normal;    // Quad: surface normal. Directional: direction towards the light. Spot: direction it shines
                alignas(16) glm::vec3 tangentX;  // Quad: left-to-right vector spanning half the width. Spot: x = cos(outer cone angle), y = cos(inner)
                alignas(16) glm::vec3 tangentY;  // Back-to-front vector spanning half the height
                alignas(16) glm::vec3 color;     // Quad: emitted radiance. Directional: irradiance on a surface facing the light. Point / spot: intensity (irradiance at 1 m)
                alignas(4)  float area;          // Quad: width * height. Directional: angular radius of the disc in radians. Point / spot: bulb radius (m)
                alignas(4)  int type;            // Fox::Core::Loaders::GLTF::LightType
            };
        }
    }
}