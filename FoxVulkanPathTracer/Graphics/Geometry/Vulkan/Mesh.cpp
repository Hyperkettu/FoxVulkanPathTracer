#include "FoxRenderer.h"

#include <map>

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

                    if (!Fox::Core::Loaders::GLTF::GLTFLoader::LoadGLTFBinaryFile(filePath, vertices, indices, materials, textures, lights, cameras, instances, submeshes)) {
                        throw std::runtime_error("Failed to load model: " + filePath);
                    }

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
                        submesh.materialIndex = submeshes[i].materialIndex;
                        this->gpuSubmeshes.push_back(submesh);
                    }

                    std::map<int, int> textureMap;
                    textureMap[-1] = -1;

                    int textureSlot = 0;
                    for (const auto& tex : textures) {
                        auto texture = Fox::Graphics::Vulkan::ShaderResourceTexture::LoadFromPixelData(device, physicalDevice, commandPool->Get(), graphicsQueue,
                            tex.pixelData.data(), static_cast<uint32_t>(tex.width), static_cast<uint32_t>(tex.height),
                            tex.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM, static_cast<uint32_t>(tex.componentCount));
                        textureMap[textureSlot++] = static_cast<int>(Fox::Graphics::Managers::Vulkan::TextureManager::Get().AddBindlessTexture(std::move(texture)));
                    }

                    for (auto i = 0; i < materials.size(); i++) {
                        Fox::Graphics::Vulkan::Material mat;
                        mat.albedo = glm::vec4(materials[i].baseColorFactor[0], materials[i].baseColorFactor[1], materials[i].baseColorFactor[2], materials[i].baseColorFactor[3]);
                        mat.roughnessMetallic = glm::vec2(materials[i].roughnessFactor, materials[i].metallicFactor);

                        mat.albedoTextureIndex = textureMap[materials[i].baseColorTextureIndex];
                        mat.roughnessMetallicTextureIndex = textureMap[materials[i].metallicRoughnessTextureIndex];

                        mat.emissiveColor = glm::vec3(materials[i].emissiveFactor[0], materials[i].emissiveFactor[1], materials[i].emissiveFactor[2]);
                        mat.emissiveTextureIndex = textureMap[materials[i].emissiveTextureIndex];
                        mat.intensity = materials[i].emissiveStrength;

                        this->materials.push_back(mat);
                    }

                    for (auto i = 0; i < lights.size(); i++) {
                        auto light = lights[i];

                        Fox::Graphics::Vulkan::Light l{};
                        l.type = static_cast<int>(light.type);
                        l.color = light.color * light.intensity;

                        if (light.type == Fox::Core::Loaders::GLTF::LightType::QuadAreaLight) {
                            l.area = light.area;
                            l.position = light.position;
                            l.normal = light.normal;
                            l.tangentX = light.tangentX;
                            l.tangentY = light.tangentY;
                        }
                        else if (light.type == Fox::Core::Loaders::GLTF::LightType::Directional) {
                            // The shader expects the direction *towards* the light in the normal field.
                            l.normal = -glm::normalize(glm::vec3(light.direction[0], light.direction[1], light.direction[2]));
                        }
                        else {
                            // Point and spot lights are parsed but not shaded yet (see closest_hit.slang).
                            continue;
                        }

                        this->lights.push_back(l);
                    }


               
                }

            }
        }
    }
}