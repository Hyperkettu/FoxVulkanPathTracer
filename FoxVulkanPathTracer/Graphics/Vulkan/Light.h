#pragma once 

#include <glm/glm.hpp>

namespace Fox {

    namespace Graphics {

        namespace Vulkan {

            struct Light {
                alignas(16) glm::vec3 position;  // Center of the quad
                alignas(16) glm::vec3 normal;    // Facing direction of the light (usually down: 0, -1, 0)
                alignas(16) glm::vec3 tangentX;  // Left-to-right vector spanning half the width
                alignas(16) glm::vec3 tangentY;  // Back-to-front vector spanning half the height
                alignas(16) glm::vec3 color;     // Emissive factor * emissive strength
                alignas(4)  float area;          // Width * Height of the quad
                alignas(4)  int type;            // 2 = Quad Area Light
            };
        }
    }
}