#pragma once

#include <unordered_map>

namespace Fox {

	namespace Graphics {

		namespace Managers {

			namespace Vulkan {

				enum class MeshResource : int32_t {
					SUBDIVIDED_PLANE = 0,
					CUBE = 1,
					SPHERE = 2
				};

				class MeshManager : public Fox::Core::Singleton<Fox::Graphics::Managers::Vulkan::MeshManager> {  
					friend class Singleton <Fox::Graphics::Managers::Vulkan::MeshManager>;

				public: 

					MeshManager() = default;
					~MeshManager() = default;

					bool Initialize(VkDevice device);

					void Destroy() {
						meshes.clear();
					}

					inline std::unique_ptr<Fox::Graphics::Geometry::Vulkan::Mesh>& GetMesh(Fox::Graphics::Managers::Vulkan::MeshResource id) {
						return meshes[id];
					}

				private:
					std::unordered_map<Fox::Graphics::Managers::Vulkan::MeshResource, std::unique_ptr<Fox::Graphics::Geometry::Vulkan::Mesh>> meshes;
				};
			}
		}
	}
}