#include "FoxRenderer.h"

namespace Fox {
    namespace Graphics {
        namespace Geometry {
            namespace Vulkan {

                void Mesh::LoadFromGLTF(const std::string& filePath) {  
                    std::vector<Fox::Core::Loaders::GLTF::VertexData> vertices;
                    std::vector<uint32_t> indices;
                    std::vector<Fox::Core::Loaders::GLTF::MaterialData> materials;
                    std::vector<Fox::Core::Loaders::GLTF::TextureData> textures;
                    std::vector<Fox::Core::Loaders::GLTF::LightData> lights;
                    std::vector<Fox::Core::Loaders::GLTF::CameraData> cameras;
                    std::vector<Fox::Graphics::Vulkan::RayTracing::RayTracingInstance> instances; 
                    std::vector<Fox::Core::Loaders::GLTF::SubmeshData> submeshes;

                    Fox::Core::Loaders::GLTF::GLTFLoader::LoadGLTFBinaryFile(filePath, vertices, indices, materials, textures, lights, cameras, instances, submeshes); 

                    this->vertices.clear();
                    this->vertices.reserve(vertices.size());
                    for (const auto& v : vertices) {
                        Fox::Graphics::Vulkan::Vertex vertex;
                        vertex.position[0] = v.position[0];
                        vertex.position[1] = v.position[1];
                        vertex.position[2] = v.position[2];
                        vertex.uv[0] = v.texCoord[0];
                        vertex.uv[1] = v.texCoord[1];
                        vertex.normal[0] = v.normal[0];
                        vertex.normal[1] = v.normal[1];
                        vertex.normal[2] = v.normal[2];
                        this->vertices.push_back(vertex);
                    }

                    this->indices = indices;
                    this->instances = instances;

                    this->submeshes.clear();
                    this->submeshes.reserve(submeshes.size());
                    for (size_t i = 0; i < submeshes.size(); i++) {
                        Fox::Graphics::Vulkan::Submesh submesh{};
                        submesh.indexOffset = submeshes[i].indexOffset;
                        submesh.indexCount = submeshes[i].indexCount;
                        submesh.vertexOffset = submeshes[i].vertexOffset;
                        submesh.vertexCount = submeshes[i].vertexCount;
                        this->gpuSubmeshes.push_back(submesh);
                    }

                    for (auto i = 0; i < materials.size(); i++) {
                        Fox::Graphics::Vulkan::Material mat;
                        mat.albedo = glm::vec4(materials[i].baseColorFactor[0], materials[i].baseColorFactor[1], materials[i].baseColorFactor[2], materials[i].baseColorFactor[3]);
                        this->materials.push_back(mat);
                    }
                }

            }
        }
    }
}