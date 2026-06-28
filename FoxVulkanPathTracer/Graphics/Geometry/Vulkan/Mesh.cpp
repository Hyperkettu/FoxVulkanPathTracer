#include "FoxRenderer.h"

#include <vector>
#include <string>
#include <iostream>
#include <cstdint>
#include <algorithm>
#include <vulkan/vulkan.h> // Required for VkTransformMatrixKHR

// Define implementation flags in exactly ONE compilation unit (.cpp file)
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "FoxVulkanPathTracer/Core/TinyGLTF/tinygltf.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

// --- Structures ---

struct VertexData {
    float position[3];
    float normal[3];
    float texCoord[2];
};

struct TextureData {
    std::vector<unsigned char> pixelData;
    int width = 0;
    int height = 0;
    int componentCount = 0;
};

struct MaterialData {
    float baseColorFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    int baseColorTextureIndex = -1;
};

enum class LightType { Directional, Point, Spot };

struct LightData {
    std::string name;
    LightType type = LightType::Point;
    float color[3] = { 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float range = 0.0f;
    float innerConeAngle = 0.0f;
    float outerConeAngle = 0.785398f;
    float position[3] = { 0.0f, 0.0f, 0.0f };
    float direction[3] = { 0.0f, 0.0f, -1.0f };
};

enum class CameraType { Perspective, Orthographic };

struct CameraData {
    std::string name;
    CameraType type = CameraType::Perspective;
    float yfov = 0.785398f;
    float aspectRatio = 1.6f;
    float xmag = 1.0f;
    float ymag = 1.0f;
    float znear = 0.1f;
    float zfar = 100.0f;
    float position[3] = { 0.0f, 0.0f, 0.0f };
    float targetDirection[3] = { 0.0f, 0.0f, -1.0f };
    float upDirection[3] = { 0.0f, 1.0f, 0.0f };
};


// --- Math Helpers ---

// Converts glTF 4x4 column-major matrix to Vulkan Ray Tracing 3x4 row-major matrix
VkTransformMatrixKHR ConvertMatrixToVulkanRT(const double m[16]) {
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

VkTransformMatrixKHR TransposeVkTransformToVkTransform(const VkTransformMatrixKHR& inMatrix) {
    VkTransformMatrixKHR outMatrix;

    // Row 0
    outMatrix.matrix[0][0] = inMatrix.matrix[0][0];
    outMatrix.matrix[0][1] = inMatrix.matrix[1][0];
    outMatrix.matrix[0][2] = inMatrix.matrix[2][0];
    outMatrix.matrix[0][3] = inMatrix.matrix[0][3]; // Retain or map translation element

    // Row 1
    outMatrix.matrix[1][0] = inMatrix.matrix[0][1];
    outMatrix.matrix[1][1] = inMatrix.matrix[1][1];
    outMatrix.matrix[1][2] = inMatrix.matrix[2][1];
    outMatrix.matrix[1][3] = inMatrix.matrix[1][3];

    // Row 2
    outMatrix.matrix[2][0] = inMatrix.matrix[0][2];
    outMatrix.matrix[2][1] = inMatrix.matrix[1][2];
    outMatrix.matrix[2][2] = inMatrix.matrix[2][2];
    outMatrix.matrix[2][3] = inMatrix.matrix[2][3];

    return outMatrix;
}

void MultiplyMatrixVector(const double m[16], const float in[4], float out[4]) {
    out[0] = static_cast<float>(m[0] * in[0] + m[4] * in[1] + m[8] * in[2] + m[12] * in[3]);
    out[1] = static_cast<float>(m[1] * in[0] + m[5] * in[1] + m[9] * in[2] + m[13] * in[3]);
    out[2] = static_cast<float>(m[2] * in[0] + m[6] * in[1] + m[10] * in[2] + m[14] * in[3]);
    out[3] = static_cast<float>(m[3] * in[0] + m[7] * in[1] + m[8] * in[2] + m[15] * in[3]);
}

void GetNodeMatrix(const tinygltf::Node& node, double matrix[16]) {
    if (node.matrix.size() == 16) {
        std::copy(node.matrix.begin(), node.matrix.end(), matrix);
        return;
    }

    // Default values
    glm::vec3 translation(0.0f);
    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f); // GLM Quat constructor: (w, x, y, z)
    glm::vec3 scale(1.0f);

    if (node.translation.size() == 3) {
        translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
    }
    if (node.rotation.size() == 4) {
        // IMPORTANT: glTF is [x,y,z,w], but GLM constructor expects (w, x, y, z)
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

    // Build the accurate column-major transformation matrix: T * R * S
    glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
    glm::mat4 R = glm::toMat4(rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);

    glm::mat4 nodeTransform = T * R * S;

    // Unpack float matrix back into the double array
    const float* pSource = glm::value_ptr(nodeTransform);
    for (int i = 0; i < 16; ++i) {
        matrix[i] = static_cast<double>(pSource[i]);
    }
}

/*
void GetNodeMatrix(const tinygltf::Node& node, double matrix[16]) {
    if (node.matrix.size() == 16) {
        std::copy(node.matrix.begin(), node.matrix.end(), matrix);
        return;
    }

    // Fallback: Build raw matrix from explicit Translation, Rotation (quaternion), and Scale
    std::fill(matrix, matrix + 16, 0.0);
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0;

    double t[3] = { 0.0, 0.0, 0.0 };
    double r[4] = { 0.0, 0.0, 0.0, 1.0 };
    double s[3] = { 1.0, 1.0, 1.0 };

    if (node.translation.size() == 3) std::copy(node.translation.begin(), node.translation.end(), t);
    if (node.rotation.size() == 4)    std::copy(node.rotation.begin(), node.rotation.end(), r);
    if (node.scale.size() == 3)       std::copy(node.scale.begin(), node.scale.end(), s);

    double qx = r[0], qy = r[1], qz = r[2], qw = r[3];
    double rMat[16] = {
        1.0 - 2.0 * qy * qy - 2.0 * qz * qz,       2.0 * qx * qy - 2.0 * qz * qw,       2.0 * qx * qz + 2.0 * qy * qw, 0.0,
              2.0 * qx * qy + 2.0 * qz * qw, 1.0 - 2.0 * qx * qx - 2.0 * qz * qz,       2.0 * qy * qz - 2.0 * qx * qw, 0.0,
              2.0 * qx * qz - 2.0 * qy * qw,       2.0 * qy * qz + 2.0 * qx * qw, 1.0 - 2.0 * qx * qx - 2.0 * qy * qy, 0.0,
                                0.0,                         0.0,                         0.0, 1.0
    };

    matrix[0] = rMat[0] * s[0];  matrix[4] = rMat[4] * s[1];  matrix[8] = rMat[8] * s[2];  matrix[12] = t[0];
    matrix[1] = rMat[1] * s[0];  matrix[5] = rMat[5] * s[1];  matrix[9] = rMat[9] * s[2];  matrix[13] = t[1];
    matrix[2] = rMat[2] * s[0];  matrix[6] = rMat[6] * s[1];  matrix[10] = rMat[10] * s[2]; matrix[14] = t[2];
    matrix[3] = 0.0;             matrix[7] = 0.0;             matrix[11] = 0.0;             matrix[15] = 1.0;
}*/

void MultiplyMatrices(const double a[16], const double b[16], double out[16]) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[i * 4 + j] = 0.0;
            for (int k = 0; k < 4; ++k) {
                out[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
            }
        }
    }
} 

// --- Recursive Traversal Function ---

void TraverseNodes(const tinygltf::Model& model, int nodeIdx, const double parentMatrix[16],
    std::vector<VertexData>& outVertices, std::vector<uint32_t>& outIndices,
    std::vector<MaterialData>& outMaterials, std::vector<TextureData>& outTextures,
    std::vector<LightData>& outLights, std::vector<CameraData>& outCameras,
    std::vector<RayTracingInstance>& outInstances, std::vector<int>& gltfImageMap)
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

    // 1. Cameras (Dynamic Positions call for World Transforms)
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

    // 2. Lights (Punctual Extensions require global placement math)
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

    // 3. Mesh Geometry (LOCAL SPACE PROCESSING FOR RAY TRACING BLAS)
    if (node.mesh >= 0 && node.mesh < model.meshes.size()) {
        const auto& mesh = model.meshes[node.mesh];
        for (const auto& primitive : mesh.primitives) {
            uint32_t vertexOffset = static_cast<uint32_t>(outVertices.size());
            uint32_t indexOffset = static_cast<uint32_t>(outIndices.size());

            // --- Material Extraction ---
            MaterialData currentMaterial;
            if (primitive.material >= 0 && primitive.material < model.materials.size()) {
                const auto& mat = model.materials[primitive.material];
                const auto& pbr = mat.pbrMetallicRoughness;
                currentMaterial.baseColorFactor[0] = static_cast<float>(pbr.baseColorFactor[0]);
                currentMaterial.baseColorFactor[1] = static_cast<float>(pbr.baseColorFactor[1]);
                currentMaterial.baseColorFactor[2] = static_cast<float>(pbr.baseColorFactor[2]);
                currentMaterial.baseColorFactor[3] = static_cast<float>(pbr.baseColorFactor[3]);

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
            }
            outMaterials.push_back(currentMaterial);
            uint32_t materialIdx = static_cast<uint32_t>(outMaterials.size() - 1);

            // --- Local Vertex Coordinate Extraction (NO WORLD MULTIPLICATION) ---
            if (primitive.attributes.find("POSITION") == primitive.attributes.end()) continue;

            const auto& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
            const auto& posView = model.bufferViews[posAccessor.bufferView];
            const float* posData = reinterpret_cast<const float*>(&(model.buffers[posView.buffer].data[posView.byteOffset + posAccessor.byteOffset]));
            size_t posStride = posView.byteStride ? posView.byteStride / sizeof(float) : 3;

            const float* normData = nullptr; size_t normStride = 0;
            if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
                const auto& normAccessor = model.accessors[primitive.attributes.at("NORMAL")];
                const auto& normView = model.bufferViews[normAccessor.bufferView];
                normData = reinterpret_cast<const float*>(&(model.buffers[normView.buffer].data[normView.byteOffset + normAccessor.byteOffset]));
                normStride = normView.byteStride ? normView.byteStride / sizeof(float) : 3;
            }

            const float* texData = nullptr; size_t texStride = 0;
            if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
                const auto& texAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
                const auto& texView = model.bufferViews[texAccessor.bufferView];
                texData = reinterpret_cast<const float*>(&(model.buffers[texView.buffer].data[texView.byteOffset + texAccessor.byteOffset]));
                texStride = texView.byteStride ? texView.byteStride / sizeof(float) : 2;
            }

            for (size_t i = 0; i < posAccessor.count; ++i) {
                VertexData v{};
                // Retain local space metrics for safe bounding/BLAS instantiation
                v.position[0] = posData[i * posStride + 0];
                v.position[1] = posData[i * posStride + 1];
                v.position[2] = posData[i * posStride + 2];

                if (normData) {
                    v.normal[0] = normData[i * normStride + 0];
                    v.normal[1] = normData[i * normStride + 1];
                    v.normal[2] = normData[i * normStride + 2];
                }
                if (texData) {
                    v.texCoord[0] = texData[i * texStride + 0];
                    v.texCoord[1] = texData[i * texStride + 1];
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
                    outIndices.push_back(idx + vertexOffset);
                }
            } else {
                indexCountForPrimitive = static_cast<uint32_t>(posAccessor.count);
                for (uint32_t i = 0; i < indexCountForPrimitive; ++i) {
                    outIndices.push_back(i + vertexOffset);
                }
            }

            // --- Record Ray Tracing Instance Info ---
            // Save out matrix converted to Vulkan format for the TLAS step
            RayTracingInstance instanceMetadata;
            instanceMetadata.firstIndex = indexOffset;
            instanceMetadata.indexCount = indexCountForPrimitive;
            instanceMetadata.materialIndex = materialIdx;
            instanceMetadata.transform = (ConvertMatrixToVulkanRT(worldMatrix));

            outInstances.push_back(instanceMetadata);
        }
    }

    // Recurse down children
    for (int childId : node.children) {
        TraverseNodes(model, childId, worldMatrix, outVertices, outIndices, outMaterials, outTextures, outLights, outCameras, outInstances, gltfImageMap);
    }
}

// --- Main Wrapper Endpoint ---

bool LoadGLTFBinaryFile(const std::string& filename,
    std::vector<VertexData>& outVertices, std::vector<uint32_t>& outIndices,
    std::vector<MaterialData>& outMaterials, std::vector<TextureData>& outTextures,
    std::vector<LightData>& outLights, std::vector<CameraData>& outCameras,
    std::vector<RayTracingInstance>& outInstances)
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
                outLights, outCameras, outInstances, gltfImageMap);
        }
    }
    return true;
}
namespace Fox {

	namespace Graphics {

		namespace Geometry {

			namespace Vulkan {

				void Mesh::Load(const std::string& filePath) {
                    std::vector<VertexData> vertices;
                    std::vector<uint32_t> indices;
                    std::vector<MaterialData> materials;
                    std::vector<TextureData> textures;
                    std::vector<LightData> lights;
                    std::vector<CameraData> cameras;
                    std::vector<RayTracingInstance> instances;

                    LoadGLTFBinaryFile(filePath, vertices, indices, materials, textures, lights, cameras, instances); 
				
                    for (auto v : vertices) {
                        Fox::Graphics::Vulkan::Vertex vertex;
                        vertex.position = glm::vec3(v.position[0], v.position[1], v.position[2]);
                        vertex.uv = glm::vec2(v.texCoord[0], v.texCoord[1]);
                        vertex.normal = glm::vec3(v.normal[0], v.normal[1], v.normal[2]);
                        this->vertices.push_back(vertex);
                        
                    }

                    this->indices = indices;
                    this->instances = instances;
                        
                }

			}
		}
	}
}
