#pragma once

#include <glm/glm.hpp>

#include "FoxVulkanPathTracer/Graphics/Vulkan/Raytracing/RaytracingInstance.h"

namespace tinygltf {
    class Model;
    class Node;
}

namespace Fox {

	namespace Core {
	
		namespace Loaders {

            namespace GLTF {

                struct VertexData {
                    float position[3];
                    float normal[3];
                    float texCoord[2];
                    float tangent[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // xyz + handedness; all zero if the file has none
                };

                struct SubmeshData {
                    uint32_t vertexOffset;
                    uint32_t indexOffset;
                    uint32_t vertexCount;
                    uint32_t indexCount;
                    uint32_t materialIndex;
                };

                struct TextureData {
                    std::vector<unsigned char> pixelData;
                    int width = 0;
                    int height = 0;
                    int componentCount = 0;
                    bool srgb = true; // Colour data (base colour, emissive) is sRGB; data textures are linear
                };

                struct MaterialData {
                    std::string name;
                    float baseColorFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                    int baseColorTextureIndex = -1;
                    float emissiveFactor[3] = { 0.0f, 0.0f, 0.0f };
                    int emissiveTextureIndex = -1;
                    float emissiveStrength = 1.0f;
                    float roughnessFactor = 1.0f;
                    float metallicFactor = 1.0f;
                    int metallicRoughnessTextureIndex = -1;

                    int normalTextureIndex = -1;
                    float normalScale = 1.0f;

                    float transmissionFactor = 0.0f; // KHR_materials_transmission
                    float specularFactor = 1.0f;     // KHR_materials_specular
                    float ior = 1.5f;                // KHR_materials_ior
                    float thicknessFactor = 0.0f;    // KHR_materials_volume: > 0 turns a transmissive surface into a solid
                    float attenuationColor[3] = { 1.0f, 1.0f, 1.0f };
                    float attenuationDistance = 0.0f; // 0 = infinite: no absorption

                    // KHR_texture_transform of the base colour texture, applied to all textures of the material
                    float uvScale[2] = { 1.0f, 1.0f };
                    float uvRotation = 0.0f;

                    int lightIndex = -1; // Index into the light list if this surface is also sampled as a light
                };

                enum class LightType { Directional, Point, Spot, QuadAreaLight, EmissiveTriangle };

                struct LightData {
                    std::string name;
                    LightType type = LightType::Point;
                    glm::vec3 color = { 1.0f, 1.0f, 1.0f };
                    float intensity = 1.0f;
                    float range = 0.0f;
                    float innerConeAngle = 0.0f;
                    float outerConeAngle = 0.785398f;
                    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
                    float direction[3] = { 0.0f, 0.0f, -1.0f };
                    glm::vec3 normal;
                    glm::vec3 tangentX;
                    glm::vec3 tangentY;
                    float area;
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

                class GLTFLoader {
                public:
                    static bool LoadGLTFBinaryFile(const std::string& filename,
                        std::vector<VertexData>& outVertices, std::vector<uint32_t>& outIndices,
                        std::vector<MaterialData>& outMaterials, std::vector<TextureData>& outTextures,
                        std::vector<LightData>& outLights, std::vector<CameraData>& outCameras,
                        std::vector<Fox::Graphics::Vulkan::RayTracing::RayTracingInstance>& outInstances,
                        std::vector<SubmeshData>& outSubmeshes
                    );

                    static VkTransformMatrixKHR glmToVkTransformMatrix(const glm::mat4& matrix)
                    {
                        // Transpose the matrix first to convert it from column-major to row-major
                        glm::mat4 rowMajor = glm::transpose(matrix);

                        VkTransformMatrixKHR outMatrix;

                        // Copy the top 3 rows (which now contain the original translation and rotation columns)
                        std::memcpy(&outMatrix.matrix, &rowMajor, sizeof(VkTransformMatrixKHR));

                        return outMatrix;
                    }

                private:
                    static VkTransformMatrixKHR ConvertMatrixToVulkanRT(const double m[16]);
                    static glm::mat4 ArrayToGlmMat4(const double m[16]);
                    static Fox::Core::Loaders::GLTF::LightData CreateQuadAreaLightFromGLTF(const std::vector<VertexData>& primitiveVertices,
                        const MaterialData& material,
                        const double worldMatrixArray[16]);

                    static void MultiplyMatrixVector(const double m[16], const float in[4], float out[4]);
                    static void GetNodeMatrix(const tinygltf::Node& node, double matrix[16]);
                    static void MultiplyMatrices(const double a[16], const double b[16], double out[16]);
                    static void TraverseNodes(const tinygltf::Model& model, int nodeIdx, const double parentMatrix[16],
                        std::vector<VertexData>& outVertices, std::vector<uint32_t>& outIndices,
                        std::vector<MaterialData>& outMaterials, std::vector<TextureData>& outTextures,
                        std::vector<LightData>& outLights, std::vector<CameraData>& outCameras,
                        std::vector<Fox::Graphics::Vulkan::RayTracing::RayTracingInstance>& outInstances,
                        std::vector<SubmeshData>& outSubmeshes, std::vector<int>& gltfImageMap);

                };
            }
		}
	}
}