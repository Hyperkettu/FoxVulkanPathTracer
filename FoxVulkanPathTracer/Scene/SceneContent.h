#pragma once

#include <string>
#include <memory>

namespace Fox {

	namespace Scene {

		// A simple material for generated surfaces (ground, spheres)
		struct SurfaceMaterial {
			glm::vec3 color{ 0.8f };
			float roughness = 0.5f;
			float metallic = 0.0f;
			float transmission = 0.0f;           // 1: glass
			float ior = 1.5f;                    // Glass: solid (refracting) when > 0
			glm::vec3 attenuationColor{ 1.0f };  // Glass: colour left after attenuationDistance metres
			float attenuationDistance = 0.0f;    // 0: clear

			Fox::Graphics::Vulkan::Material ToMaterial() const;
		};

		// Image files of one surface (paths from the working directory; empty = not used)
		struct SurfaceTextures {
			std::string albedo, normal, roughness, metallic, occlusion, displacement;
			bool normalGreenUp = true;       // Normal map convention: OpenGL (true) or DirectX
			bool invertDisplacement = false; // Bright = low
		};

		// How a tree looks: its own textures and factors for bark and leaves
		struct TreeLook {
			SurfaceTextures bark{ "Textures/bark/albedo.png", "Textures/bark/normal.png", "Textures/bark/roughness.png",
				"Textures/bark/metallic.png", "Textures/bark/ao.png", "Textures/bark/displacement.png", true, false };
			SurfaceTextures leaf{ "Textures/bark/leaf-albedo.png", "Textures/bark/leaf_normal.png", "Textures/bark/leaf-roughness.png",
				"Textures/bark/leaf-metallic.png", "Textures/bark/leaf_ambient_occlusion.png", "Textures/bark/leaf_displacement.png", false, true };
			glm::vec3 barkTint{ 1.0f };
			glm::vec3 leafTint{ 1.0f };
			float barkRoughness = 1.0f; // Multiplies the roughness texture
			float leafRoughness = 1.0f;
			float alphaCutoff = 0.5f;
		};

		// A generated tree (the mesh of a tree MeshComponent). Its name and placement are its entity's.
		struct Tree {
			Fox::Graphics::Geometry::TreeSettings shape; // Its height maps come from the look
			TreeLook look;
			Fox::Animation::WindSkeleton::Settings wind;

			// Set by the scene
			bool regenerate = true; // The shape (or a texture the geometry depends on) changed
			std::shared_ptr<Fox::Graphics::Geometry::SkinnedMesh> mesh;
			std::shared_ptr<Fox::Graphics::Geometry::ClusterLod> lod; // Cluster level of detail of mesh (when the GPU has cluster structures)
			std::unique_ptr<Fox::Animation::WindSkeleton> skeleton;
			Fox::Scene::RayTracing::RayTracingScene::DynamicMeshHandle handle;
			bool placed = false;
			float generationSeconds = 0.0f;

			// A new tree with the same settings (generated anew)
			std::shared_ptr<Tree> CloneSettings() const {
				auto copy = std::make_shared<Tree>();
				copy->shape = shape;
				copy->look = look;
				copy->wind = wind;
				return copy;
			}
		};
	}
}
