glslangValidator -V --target-env vulkan1.4 -S mesh triangle.mesh -o triangle.spv
glslangValidator -V --target-env vulkan1.4 -S mesh cube.mesh -o cube.spv
glslangValidator -V --target-env vulkan1.4 -S frag basic.frag -o basic_fragment.spv
glslangValidator -V --target-env vulkan1.4 -S frag basic_color.frag -o basic_color_fragment.spv
glslangValidator -V --target-env vulkan1.4 -S mesh texturedCube.mesh -o texturedCube.spv
glslangValidator -V --target-env vulkan1.4 -S frag textured.frag -o textured_frag.spv
glslangValidator -V --target-env vulkan1.4 -S mesh generic_mesh.mesh -o generic_mesh.spv
glslangValidator -V --target-env vulkan1.4 -S frag textured_icosahedron.frag -o textured_icosahedron.spv
glslangValidator -V --target-env vulkan1.4 -S mesh multi_generic_mesh.mesh -o multi_generic_mesh.spv
glslangValidator -V --target-env vulkan1.4 -S frag textured_multi_generic_mesh.frag -o textured_multi_generic_mesh.spv
glslangValidator -V --target-env vulkan1.3 -S rgen raygen.rgen -o raygen.spv
glslangValidator -V --target-env vulkan1.3 -S rmiss miss.rmiss -o miss.spv
glslangValidator -V --target-env vulkan1.3 -S rchit closest_hit.rchit -o closestHit.spv


pause