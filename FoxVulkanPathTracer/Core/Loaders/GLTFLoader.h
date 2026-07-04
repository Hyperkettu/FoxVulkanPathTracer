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
                };

                struct SubmeshData {
                    uint32_t vertexOffset;
                    uint32_t indexOffset;
                    uint32_t vertexCount;
                    uint32_t indexCount;
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
                    float emissiveFactor[3] = { 0.0f, 0.0f, 0.0f };
                    int emissiveTextureIndex = -1;
                    float emissiveStrength = 1.0f; 
                    float roughnessFactor = 1.0f;
                    float metallicFactor = 1.0f;
                    int metallicRoughnessTextureIndex = -1;
                };

                enum class LightType { Directional, Point, Spot, QuadAreaLight };

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