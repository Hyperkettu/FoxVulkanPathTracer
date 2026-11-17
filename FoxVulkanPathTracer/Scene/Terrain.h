#pragma once

#include <array>
#include <string>
#include <vector>

namespace Fox {

	namespace Scene {

		// Layers a terrain blends between: one per channel (RGBA) of its blend map
		static constexpr int TERRAIN_LAYERS = 4;

		// The textures of one ground type, and how they lie on the terrain
		struct TerrainLayer {
			std::string name;
			std::string albedo, normal, roughness, metallic, occlusion, height; // Image files; empty = not used
			bool normalGreenUp = true;  // Normal map convention: OpenGL (true) or DirectX
			float tiling = 3.0f;        // Meters covered by one repeat of the textures
			glm::vec3 tint{ 1.0f };     // Multiplies the albedo
			float roughnessScale = 1.0f; // Multiplies the roughness texture
			float normalStrength = 1.0f;

			// albedo.png, normal.png, roughness.png, metallic.png, ao.png and displacement.png from a folder
			static TerrainLayer FromFolder(const std::string& folder);
		};

		struct TerrainSettings {
			float size = 400.0f;        // Meters along each side (a square)
			int resolution = 513;       // Height samples (vertices) along each side
			int blendResolution = 1024; // Blend map texels along each side
			glm::vec3 position{ 0.0f, -3.73f, 0.0f }; // Center; heights are measured from its y
			// Layers meet along the shapes of their height maps (pebbles above sand, ...): how far into the
			// mix one layer still shows through another. Small: crisp edges; large: soft gradients.
			float blendSoftness = 0.15f;
			std::array<TerrainLayer, TERRAIN_LAYERS> layers = DefaultLayers();

			// The ground types of Textures/terrain: grass, rock, badland, bumpy ground
			static std::array<TerrainLayer, TERRAIN_LAYERS> DefaultLayers();
			static TerrainSettings Defaults() { return TerrainSettings{}; }
		};

		enum class TerrainBrushMode { Raise, Lower, Smooth, Flatten, Paint };

		// A round brush with a smooth edge, applied continuously while the mouse button is held
		struct TerrainBrush {
			TerrainBrushMode mode = TerrainBrushMode::Raise;
			float radius = 6.0f;   // Meters
			float strength = 2.0f; // Raise / lower: meters per second at the center; others: rate per second
			float hardness = 0.2f; // 0: falls off from the center; towards 1: full strength out to the edge
			int layer = 0;         // Paint: which layer
		};

		// A height field with a blend map: heights on a square grid around settings.position, and per blend map
		// texel the weights of the layers (summing to 1). Edits mark what changed (TakeDirtyRows / TakeDirtyBlend)
		// so the renderer updates only that.
		class Terrain {
		public:
			TerrainSettings settings;

			// Flat at settings.position.y, the first layer everywhere
			void Create(const TerrainSettings& newSettings);
			bool IsCreated() const { return !heights.empty(); }
			// Changes the grid and blend map sizes, resampling what is there
			void Resize(int resolution, int blendResolution);

			float Spacing() const { return settings.size / static_cast<float>(settings.resolution - 1); }

			// World height at a horizontal position; false outside the terrain
			bool HeightAt(float x, float z, float& y) const;
			// Where a ray (world) first meets the terrain
			bool Raycast(const glm::vec3& origin, const glm::vec3& direction, glm::vec3& hit) const;

			// The whole mesh (terrain space: around the origin, place it at settings.position) and rows of it
			void BuildMesh(std::vector<Fox::Graphics::Vulkan::Vertex>& vertices, std::vector<uint32_t>& indices) const;
			void BuildRows(int firstRow, int lastRow, std::vector<Fox::Graphics::Vulkan::Vertex>& vertices) const;

			// Brush strokes: BeginStroke where the button went down (flatten keeps that height), then ApplyBrush
			// every frame for the time it was held
			void BeginStroke(const glm::vec3& point);
			void ApplyBrush(const TerrainBrush& brush, const glm::vec3& point, float deltaTime);

			// What changed since the last call: rows of vertices [firstRow, lastRow], blend texels [x0, x1) x [y0, y1)
			bool TakeDirtyRows(int& firstRow, int& lastRow);
			bool TakeDirtyBlend(int& x0, int& y0, int& x1, int& y1);
			void MarkAllDirty();

			// Blend weights as 8 bit RGBA (one channel per layer), of a region or of the whole map
			std::vector<uint8_t> BlendPixels(int x0, int y0, int x1, int y1) const;

			// Heights: raw 32 bit floats with a small header. Blend map: an RGBA PNG (editable in any paint program).
			bool SaveHeights(const std::string& path, std::string& error) const;
			bool LoadHeights(const std::string& path, std::string& error);
			bool SaveBlend(const std::string& path, std::string& error) const;
			bool LoadBlend(const std::string& path, std::string& error);

			// The blend map on the GPU (bindless index), made by the scene and kept across rebuilds
			int blendTexture = -1;
			int blendTextureResolution = 0;

		private:
			std::vector<float> heights;    // resolution x resolution, row major (rows along z), meters above position.y
			std::vector<glm::vec4> blend;  // blendResolution x blendResolution
			float strokeHeight = 0.0f;

			int dirtyRow0 = INT32_MAX, dirtyRow1 = -1;
			int dirtyX0 = INT32_MAX, dirtyY0 = INT32_MAX, dirtyX1 = -1, dirtyY1 = -1;
			mutable bool boundsValid = false;
			mutable float minHeight = 0.0f, maxHeight = 0.0f;

			float Height(int column, int row) const;
			float LocalHeight(float x, float z) const; // Bilinear, terrain space
			void HeightBounds() const;
			Fox::Graphics::Vulkan::Vertex MakeVertex(int column, int row) const;
		};
	}
}
