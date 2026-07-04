#include "FoxRenderer.h"

// Static member definitions
std::unique_ptr<Fox::Graphics::Managers::Vulkan::MeshManager> Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::MeshManager>::instance = nullptr;
std::once_flag Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::MeshManager>::initFlag;

namespace Fox {

	namespace Graphics {

		namespace Managers {

			namespace Vulkan {

				bool MeshManager::Initialize(VkDevice device) {
					
				//	auto plane = std::make_unique<Fox::Graphics::Geometry::Vulkan::Mesh>(Fox::Graphics::Geometry::GeometryGenerator::GeneratePlaneMesh(16, 9, 0.25f, 0.25f, 0.0f));
				//	meshes[Fox::Graphics::Managers::Vulkan::MeshResource::SUBDIVIDED_PLANE] = std::move(plane);
					

				//	Fox::Graphics::Geometry::GeometryGenerator::GenerateCube(verticesCube, indicesCube, meshInfoCube);


					return true;
				}

			}
		}
	}
}