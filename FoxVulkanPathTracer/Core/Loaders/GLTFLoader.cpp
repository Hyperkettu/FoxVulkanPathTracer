#include "FoxRenderer.h"
#include "FoxVulkanPathTracer/Core/Loaders/GLTFLoader.h"

#include <vector>
#include <string>
#include <iostream>
#include <cstdint>
#include <algorithm>
#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "FoxVulkanPathTracer/Core/TinyGLTF/tinygltf.h"

namespace Fox {

	namespace Core {

		namespace Loaders {

            namespace GLTF {

            bool GLTFLoader::LoadGLTFBinaryFile(const std::string& filename,
                std::vector<VertexData>& outVertices, std::vector<uint32_t>& outIndices,
                std::vector<MaterialData>& outMaterials, std::vector<TextureData>& outTextures,
                std::vector<LightData>& outLights, std::vector<CameraData>& outCameras,
                std::vector<Fox::Graphics::Vulkan::RayTracing::RayTracingInstance>& outInstances,
                std::vector<SubmeshData>& outSubmeshes
            )
            {
                tinygltf::Model model;
                tinygltf::TinyGLTF loader;
                std::string err, warn;

                if (!loader.LoadBinaryFromFile(&model, &err, &warn, filename)) return false;

                std::vector<int> gltfImageMap(model.images.size(), -1);
                double identityMatrix[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };

                int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
                if (model.scenes.size() > 0) {
                    const tinygltf::Scene& scene = model.scenes[sceneIdx];
                    for (int rootNodeIdx : scene.nodes) {
                        TraverseNodes(model, rootNodeIdx, identityMatrix,
                            outVertices, outIndices, outMaterials, outTextures,
                            outLights, outCameras, outInstances, outSubmeshes, gltfImageMap);
                    }
                }
                return true;
            }

            glm::mat4 GLTFLoader::ArrayToGlmMat4(const double m[16]) {
                return glm::mat4(
                    static_cast<float>(m[0]), static_cast<float>(m[1]), static_cast<float>(m[2]), static_cast<float>(m[3]),
                    static_cast<float>(m[4]), static_cast<float>(m[5]), static_cast<float>(m[6]), static_cast<float>(m[7]),
                    static_cast<float>(m[8]), static_cast<float>(m[9]), static_cast<float>(m[10]), static_cast<float>(m[11]),
                    static_cast<float>(m[12]), static_cast<float>(m[13]), static_cast<float>(m[14]), static_cast<float>(m[15])
                );
            }

            Fox::Core::Loaders::GLTF::LightData GLTFLoader::CreateQuadAreaLightFromGLTF( 
                const std::vector<VertexData>& primitiveVertices,
                const MaterialData& material,
                const double worldMatrixArray[16])
            {
                Fox::Core::Loaders::GLTF::LightData outLight{};

                // 1. Transform quad corners into World Space
                glm::mat4 worldMat = ArrayToGlmMat4(worldMatrixArray);

                // Assuming a standard quad has 4 sequential vertices forming a loop (or extracted via indices)
                glm::vec3 p0 = glm::vec3(worldMat * glm::vec4(primitiveVertices[0].position[0], primitiveVertices[0].position[1], primitiveVertices[0].position[2], 1.0f));
                glm::vec3 p1 = glm::vec3(worldMat * glm::vec4(primitiveVertices[1].position[0], primitiveVertices[1].position[1], primitiveVertices[1].position[2], 1.0f));
                glm::vec3 p2 = glm::vec3(worldMat * glm::vec4(primitiveVertices[2].position[0], primitiveVertices[2].position[1], primitiveVertices[2].position[2], 1.0f));
                glm::vec3 p3 = glm::vec3(worldMat * glm::vec4(primitiveVertices[3].position[0], primitiveVertices[3].position[1], primitiveVertices[3].position[2], 1.0f));

                // 2. Position (Geometric Center of the Quad)
                glm::vec3 position = (p0 + p1 + p2 + p3) * 0.25f;
                outLight.position = position;

                // 3. Tangent Axes
                // Calculate full vectors spanning across the quad surface edges
                glm::vec3 edgeY = p1 - p0;
                glm::vec3 edgeX = p3 - p0;

                // Your struct states tangentX/Y spans *half* the width/height
                outLight.tangentX = edgeX * 0.5f;
                outLight.tangentY = edgeY * 0.5f;

                // 4. Normal Vector
                // Surface normal derived from cross-product of the edge vectors
                outLight.normal = glm::normalize(glm::cross(edgeX, edgeY));

                // 5. Area Calculation (Magnitude of cross product yields full area of the parallelogram)
                outLight.area = glm::length(glm::cross(edgeX, edgeY));

                // 6. Color Assignment (Emissive Factor scaled by Emissive Strength)
                outLight.color.x = material.emissiveFactor[0];
                outLight.color.y = material.emissiveFactor[1];
                outLight.color.z = material.emissiveFactor[2];
                outLight.intensity = material.emissiveStrength;

                // 7. Light Type Identifier
                outLight.type = LightType::QuadAreaLight; // Quad Area Light

                return outLight;
            }

            VkTransformMatrixKHR GLTFLoader::ConvertMatrixToVulkanRT(const double m[16]) {
                VkTransformMatrixKHR outMatrix;
                outMatrix.matrix[0][0] = static_cast<float>(m[0]);
                outMatrix.matrix[0][1] = static_cast<float>(m[4]);
                outMatrix.matrix[0][2] = static_cast<float>(m[8]);
                outMatrix.matrix[0][3] = static_cast<float>(m[12]);

                outMatrix.matrix[1][0] = static_cast<float>(m[1]);
                outMatrix.matrix[1][1] = static_cast<float>(m[5]);
                outMatrix.matrix[1][2] = static_cast<float>(m[9]);
                outMatrix.matrix[1][3] = static_cast<float>(m[13]);

                outMatrix.matrix[2][0] = static_cast<float>(m[2]);
                outMatrix.matrix[2][1] = static_cast<float>(m[6]);
                outMatrix.matrix[2][2] = static_cast<float>(m[10]);
                outMatrix.matrix[2][3] = static_cast<float>(m[14]);
                return outMatrix;
            }

            void GLTFLoader::MultiplyMatrixVector(const double m[16], const float in[4], float out[4]) {
                out[0] = static_cast<float>(m[0] * in[0] + m[4] * in[1] + m[8] * in[2] + m[12] * in[3]);
                out[1] = static_cast<float>(m[1] * in[0] + m[5] * in[1] + m[9] * in[2] + m[13] * in[3]);
                out[2] = static_cast<float>(m[2] * in[0] + m[6] * in[1] + m[10] * in[2] + m[14] * in[3]);
                out[3] = static_cast<float>(m[3] * in[0] + m[7] * in[1] + m[8] * in[2] + m[15] * in[3]);
            }

            void GLTFLoader::GetNodeMatrix(const tinygltf::Node& node, double matrix[16]) {
                if (node.matrix.size() == 16) {
                    std::copy(node.matrix.begin(), node.matrix.end(), matrix);
                    return;
                }

                glm::vec3 translation(0.0f);
                glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
                glm::vec3 scale(1.0f);

                if (node.translation.size() == 3) {
                    translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
                }
                if (node.rotation.size() == 4) {
                    rotation = glm::quat(
                        static_cast<float>(node.rotation[3]), // W
                        static_cast<float>(node.rotation[0]), // X
                        static_cast<float>(node.rotation[1]), // Y
                        static_cast<float>(node.rotation[2])  // Z
                    );
                }
                if (node.scale.size() == 3) {
                    scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
                }

                glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
                glm::mat4 R = glm::toMat4(rotation);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);

                glm::mat4 nodeTransform = T * R * S;

                const float* pSource = glm::value_ptr(nodeTransform);
                for (int i = 0; i < 16; ++i) {
                    matrix[i] = static_cast<double>(pSource[i]);
                }
            }

            void GLTFLoader::MultiplyMatrices(const double a[16], const double b[16], double out[16]) {
                for (int i = 0; i < 4; ++i) {
                    for (int j = 0; j < 4; ++j) {
                        out[i * 4 + j] = 0.0;
                        for (int k = 0; k < 4; ++k) {
                            out[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
                        }
                    }
                }
            }

            void GLTFLoader::TraverseNodes(const tinygltf::Model& model, int nodeIdx, const double parentMatrix[16],
                std::vector<VertexData>& outVertices, std::vector<uint32_t>& outIndices,
                std::vector<MaterialData>& outMaterials, std::vector<TextureData>& outTextures,
                std::vector<LightData>& outLights, std::vector<CameraData>& outCameras,
                std::vector<Fox::Graphics::Vulkan::RayTracing::RayTracingInstance>& outInstances,
                std::vector<SubmeshData>& outSubmeshes, std::vector<int>& gltfImageMap)
            {
                const tinygltf::Node& node = model.nodes[nodeIdx];

                double localMatrix[16];
                GetNodeMatrix(node, localMatrix);

                double worldMatrix[16];
                MultiplyMatrices(parentMatrix, localMatrix, worldMatrix);

                float origin[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                float forward[4] = { 0.0f, 0.0f, -1.0f, 0.0f };
                float up[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
                float transformed[4];

                // 1. Cameras
                if (node.camera >= 0 && node.camera < model.cameras.size()) {
                    const tinygltf::Camera& gltfCam = model.cameras[node.camera];
                    CameraData cam;
                    cam.name = gltfCam.name;

                    if (gltfCam.type == "perspective") {
                        cam.type = CameraType::Perspective;
                        cam.yfov = static_cast<float>(gltfCam.perspective.yfov);
                        cam.aspectRatio = static_cast<float>(gltfCam.perspective.aspectRatio);
                        cam.znear = static_cast<float>(gltfCam.perspective.znear);
                        cam.zfar = static_cast<float>(gltfCam.perspective.zfar);
                    } else {
                        cam.type = CameraType::Orthographic;
                        cam.xmag = static_cast<float>(gltfCam.orthographic.xmag);
                        cam.ymag = static_cast<float>(gltfCam.orthographic.ymag);
                        cam.znear = static_cast<float>(gltfCam.orthographic.znear);
                        cam.zfar = static_cast<float>(gltfCam.orthographic.zfar);
                    }

                    MultiplyMatrixVector(worldMatrix, origin, transformed);
                    cam.position[0] = transformed[0]; cam.position[1] = transformed[1]; cam.position[2] = transformed[2];

                    MultiplyMatrixVector(worldMatrix, forward, transformed);
                    cam.targetDirection[0] = transformed[0]; cam.targetDirection[1] = transformed[1]; cam.targetDirection[2] = transformed[2];

                    MultiplyMatrixVector(worldMatrix, up, transformed);
                    cam.upDirection[0] = transformed[0]; cam.upDirection[1] = transformed[1]; cam.upDirection[2] = transformed[2];

                    outCameras.push_back(cam);
                }

                // 2. Lights (KHR_lights_punctual)
                if (node.extensions.find("KHR_lights_punctual") != node.extensions.end()) {
                    const auto& ext = node.extensions.at("KHR_lights_punctual");
                    if (ext.IsObject() && ext.Has("light")) {
                        int lightIndex = ext.Get("light").GetNumberAsInt();
                        if (lightIndex >= 0 && lightIndex < model.lights.size()) {
                            const tinygltf::Light& gltfLight = model.lights[lightIndex];
                            LightData light;
                            light.name = gltfLight.name;
                            light.intensity = static_cast<float>(gltfLight.intensity);
                            light.range = static_cast<float>(gltfLight.range);

                            light.color[0] = gltfLight.color.size() > 0 ? static_cast<float>(gltfLight.color[0]) : 1.0f;
                            light.color[1] = gltfLight.color.size() > 1 ? static_cast<float>(gltfLight.color[1]) : 1.0f;
                            light.color[2] = gltfLight.color.size() > 2 ? static_cast<float>(gltfLight.color[2]) : 1.0f;

                            if (gltfLight.type == "directional") light.type = LightType::Directional;
                            else if (gltfLight.type == "spot") {
                                light.type = LightType::Spot;
                                light.innerConeAngle = static_cast<float>(gltfLight.spot.innerConeAngle);
                                light.outerConeAngle = static_cast<float>(gltfLight.spot.outerConeAngle);
                            } else light.type = LightType::Point;

                            MultiplyMatrixVector(worldMatrix, origin, transformed);
                            light.position[0] = transformed[0]; light.position[1] = transformed[1]; light.position[2] = transformed[2];

                            MultiplyMatrixVector(worldMatrix, forward, transformed);
                            light.direction[0] = transformed[0]; light.direction[1] = transformed[1]; light.direction[2] = transformed[2];

                            outLights.push_back(light);
                        }
                    }
                }

                // 3. Mesh Geometry
                if (node.mesh >= 0 && node.mesh < model.meshes.size()) {
                    const auto& mesh = model.meshes[node.mesh];
                    for (const auto& primitive : mesh.primitives) {

                        if (primitive.attributes.find("POSITION") == primitive.attributes.end()) continue;

                        uint32_t vertexOffset = static_cast<uint32_t>(outVertices.size());
                        uint32_t indexOffset = static_cast<uint32_t>(outIndices.size());

                        // --- Material Extraction ---
                        MaterialData currentMaterial;
                        if (primitive.material >= 0 && primitive.material < model.materials.size()) {
                            const auto& mat = model.materials[primitive.material];
                            const auto& pbr = mat.pbrMetallicRoughness;

                            // 1. Base Color Factors
                            currentMaterial.baseColorFactor[0] = static_cast<float>(pbr.baseColorFactor[0]);
                            currentMaterial.baseColorFactor[1] = static_cast<float>(pbr.baseColorFactor[1]);
                            currentMaterial.baseColorFactor[2] = static_cast<float>(pbr.baseColorFactor[2]);
                            currentMaterial.baseColorFactor[3] = static_cast<float>(pbr.baseColorFactor[3]);

                            // 2. Metallic and Roughness Factors
                            currentMaterial.metallicFactor = static_cast<float>(pbr.metallicFactor);
                            currentMaterial.roughnessFactor = static_cast<float>(pbr.roughnessFactor);

                            // 3. Base Color Texture Index
                            if (pbr.baseColorTexture.index >= 0) {
                                int srcImg = model.textures[pbr.baseColorTexture.index].source;
                                if (srcImg >= 0 && srcImg < model.images.size()) {
                                    if (gltfImageMap[srcImg] != -1) {
                                        currentMaterial.baseColorTextureIndex = gltfImageMap[srcImg];
                                    } else {
                                        const auto& img = model.images[srcImg];
                                        TextureData tex{ img.image, img.width, img.height, img.component };
                                        outTextures.push_back(tex);
                                        gltfImageMap[srcImg] = static_cast<int>(outTextures.size() - 1);
                                        currentMaterial.baseColorTextureIndex = gltfImageMap[srcImg];
                                    }
                                }
                            }

                            // 4. Metallic/Roughness Texture Index (Packed texture: G = Roughness, B = Metallic)
                            if (pbr.metallicRoughnessTexture.index >= 0) {
                                int srcImg = model.textures[pbr.metallicRoughnessTexture.index].source;
                                if (srcImg >= 0 && srcImg < model.images.size()) {
                                    if (gltfImageMap[srcImg] != -1) {
                                        currentMaterial.metallicRoughnessTextureIndex = gltfImageMap[srcImg];
                                    } else {
                                        const auto& img = model.images[srcImg];
                                        TextureData tex{ img.image, img.width, img.height, img.component };
                                        outTextures.push_back(tex);
                                        gltfImageMap[srcImg] = static_cast<int>(outTextures.size() - 1);
                                        currentMaterial.metallicRoughnessTextureIndex = gltfImageMap[srcImg];
                                    }
                                }
                            } else {
                                currentMaterial.metallicRoughnessTextureIndex = -1;
                            }

                            // 5. Emissive Factors
                            if (mat.emissiveFactor.size() >= 3) {
                                currentMaterial.emissiveFactor[0] = static_cast<float>(mat.emissiveFactor[0]);
                                currentMaterial.emissiveFactor[1] = static_cast<float>(mat.emissiveFactor[1]);
                                currentMaterial.emissiveFactor[2] = static_cast<float>(mat.emissiveFactor[2]);
                            } else {
                                currentMaterial.emissiveFactor[0] = 0.0f;
                                currentMaterial.emissiveFactor[1] = 0.0f;
                                currentMaterial.emissiveFactor[2] = 0.0f;
                            }

                            // 6. KHR_materials_emissive_strength Extension Parsing
                            currentMaterial.emissiveStrength = 1.0f;
                            if (mat.extensions.find("KHR_materials_emissive_strength") != mat.extensions.end()) {
                                const auto& ext = mat.extensions.at("KHR_materials_emissive_strength");
                                if (ext.IsObject() && ext.Has("emissiveStrength")) {
                                    currentMaterial.emissiveStrength = static_cast<float>(ext.Get("emissiveStrength").GetNumberAsDouble());
                                }
                            }

                            // 7. Emissive Texture Index
                            if (mat.emissiveTexture.index >= 0) {
                                int srcImg = model.textures[mat.emissiveTexture.index].source;
                                if (srcImg >= 0 && srcImg < model.images.size()) {
                                    if (gltfImageMap[srcImg] != -1) {
                                        currentMaterial.emissiveTextureIndex = gltfImageMap[srcImg];
                                    } else {
                                        const auto& img = model.images[srcImg];
                                        TextureData tex{ img.image, img.width, img.height, img.component };
                                        outTextures.push_back(tex);
                                        gltfImageMap[srcImg] = static_cast<int>(outTextures.size() - 1);
                                        currentMaterial.emissiveTextureIndex = gltfImageMap[srcImg];
                                    }
                                }
                            } else {
                                currentMaterial.emissiveTextureIndex = -1;
                            }
                        }

                        outMaterials.push_back(currentMaterial); 
                        uint32_t materialIdx = static_cast<uint32_t>(outMaterials.size() - 1); 

                        // --- Robust Byte-Stride Attribute Windows Extraction ---
                        const auto& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
                        const auto& posView = model.bufferViews[posAccessor.bufferView];
                        const unsigned char* posBufferBytes = &model.buffers[posView.buffer].data[posView.byteOffset];
                        size_t posByteStride = posView.byteStride ? posView.byteStride : 3 * sizeof(float);

                        const unsigned char* normBufferBytes = nullptr;
                        size_t normByteStride = 3 * sizeof(float);
                        size_t normAccessorByteOffset = 0;
                        if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
                            const auto& normAccessor = model.accessors[primitive.attributes.at("NORMAL")];
                            const auto& normView = model.bufferViews[normAccessor.bufferView];
                            normBufferBytes = &model.buffers[normView.buffer].data[normView.byteOffset];
                            normByteStride = normView.byteStride ? normView.byteStride : 3 * sizeof(float);
                            normAccessorByteOffset = normAccessor.byteOffset;
                        }

                        const unsigned char* texBufferBytes = nullptr;
                        size_t texByteStride = 2 * sizeof(float);
                        size_t texAccessorByteOffset = 0;
                        if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
                            const auto& texAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
                            const auto& texView = model.bufferViews[texAccessor.bufferView];
                            texBufferBytes = &model.buffers[texView.buffer].data[texView.byteOffset];
                            texByteStride = texView.byteStride ? texView.byteStride : 2 * sizeof(float);
                            texAccessorByteOffset = texAccessor.byteOffset;
                        }

                        // Loop and resolve individual element addresses safely
                        for (size_t i = 0; i < posAccessor.count; ++i) {
                            VertexData v{};

                            const float* pPos = reinterpret_cast<const float*>(posBufferBytes + posAccessor.byteOffset + (i * posByteStride));
                            v.position[0] = pPos[0];
                            v.position[1] = pPos[1];
                            v.position[2] = pPos[2];

                            if (normBufferBytes) {
                                const float* pNorm = reinterpret_cast<const float*>(normBufferBytes + normAccessorByteOffset + (i * normByteStride));
                                v.normal[0] = pNorm[0];
                                v.normal[1] = pNorm[1];
                                v.normal[2] = pNorm[2];
                            }
                            if (texBufferBytes) {
                                const float* pTex = reinterpret_cast<const float*>(texBufferBytes + texAccessorByteOffset + (i * texByteStride));
                                v.texCoord[0] = pTex[0];
                                v.texCoord[1] = pTex[1];
                            }
                            outVertices.push_back(v);
                        }

                        // --- Index Extraction ---
                        uint32_t indexCountForPrimitive = 0;
                        if (primitive.indices >= 0) {
                            const auto& idxAccessor = model.accessors[primitive.indices];
                            const auto& view = model.bufferViews[idxAccessor.bufferView];
                            const unsigned char* dataPtr = &(model.buffers[view.buffer].data[view.byteOffset + idxAccessor.byteOffset]);
                            indexCountForPrimitive = static_cast<uint32_t>(idxAccessor.count);

                            for (size_t i = 0; i < idxAccessor.count; ++i) {
                                uint32_t idx = 0;
                                if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) idx = reinterpret_cast<const uint32_t*>(dataPtr)[i];
                                else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) idx = reinterpret_cast<const uint16_t*>(dataPtr)[i];
                                else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) idx = reinterpret_cast<const uint8_t*>(dataPtr)[i];

                                // Bake vertex offset directly into the flat global array
                                outIndices.push_back(idx + vertexOffset);
                            }
                        } else {
                            // Generate unindexed sequential stream mapping if indices missing
                            indexCountForPrimitive = static_cast<uint32_t>(posAccessor.count);
                            for (uint32_t i = 0; i < indexCountForPrimitive; ++i) {
                                outIndices.push_back(i + vertexOffset);
                            }
                        }

                        bool isEmissive = (currentMaterial.emissiveFactor[0] > 0.0f ||
                            currentMaterial.emissiveFactor[1] > 0.0f ||
                            currentMaterial.emissiveFactor[2] > 0.0f);

                        // Check if it's an emissive quad mesh (4 vertices parsed for this primitive, or index count is 6)
                        if (isEmissive && posAccessor.count == 4)
                        {
                            // Wait until the vertex addresses parsing loop finishes loading local vertices, then:
                            std::vector<VertexData> quadVertices(outVertices.end() - 4, outVertices.end());

                            Fox::Core::Loaders::GLTF::LightData quadLight = CreateQuadAreaLightFromGLTF(quadVertices, currentMaterial, worldMatrix);
                            outLights.push_back(quadLight);
                        }

                        // --- Record Ray Tracing Instance Info & Submesh Configuration ---
                        Fox::Graphics::Vulkan::RayTracing::RayTracingInstance instanceMetadata;
                        instanceMetadata.firstIndex = indexOffset;
                        instanceMetadata.indexCount = indexCountForPrimitive;
                        instanceMetadata.materialIndex = materialIdx;
                        instanceMetadata.transform = ArrayToGlmMat4(worldMatrix);

                        Fox::Core::Loaders::GLTF::SubmeshData submesh;
                        submesh.indexOffset = indexOffset;
                        submesh.vertexOffset = vertexOffset;
                        submesh.indexCount = indexCountForPrimitive;
                        submesh.vertexCount = static_cast<uint32_t>(posAccessor.count);
                        submesh.materialIndex = materialIdx;

                        outSubmeshes.push_back(submesh);
                        outInstances.push_back(instanceMetadata);
                    }
                }

                // Recurse children hierarchies
                for (int childId : node.children) {
                    TraverseNodes(model, childId, worldMatrix, outVertices, outIndices, outMaterials, outTextures, outLights, outCameras, outInstances, outSubmeshes, gltfImageMap);
                }
               }
            }
		}
	}
}