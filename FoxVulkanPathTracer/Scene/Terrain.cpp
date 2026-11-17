#include "FoxRenderer.h"
#include "FoxVulkanPathTracer/Core/TinyGLTF/stb_image.h"
#include "FoxVulkanPathTracer/Core/TinyGLTF/stb_image_write.h"

#include <filesystem>
#include <fstream>

namespace Fox {

	namespace Scene {

		namespace {
			// The brush's smooth edge: 1 inside the hard core, easing to 0 at the rim. d: distance / radius.
			float Falloff(float d, float hardness) {
				const float t = glm::clamp((d - hardness) / std::max(1.0f - hardness, 1e-3f), 0.0f, 1.0f);
				return 1.0f - t * t * (3.0f - 2.0f * t);
			}

			// Bilinear resampling of a square grid of values to another size (corner samples stay at the corners)
			template<typename T>
			std::vector<T> ResampleCorners(const std::vector<T>& source, int from, int to) {
				std::vector<T> result(static_cast<size_t>(to) * to);
				for (int y = 0; y < to; y++) {
					for (int x = 0; x < to; x++) {
						const float fx = static_cast<float>(x) * (from - 1) / std::max(to - 1, 1);
						const float fy = static_cast<float>(y) * (from - 1) / std::max(to - 1, 1);
						const int x0 = std::min(static_cast<int>(fx), from - 2), y0 = std::min(static_cast<int>(fy), from - 2);
						const float tx = fx - x0, ty = fy - y0;
						auto at = [&](int i, int j) { return source[static_cast<size_t>(j) * from + i]; };
						result[static_cast<size_t>(y) * to + x] = (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty)
							+ (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
					}
				}
				return result;
			}

			const uint32_t HEIGHTS_MAGIC = 0x48584F46; // "FOXH"
		}

		// --- Settings ---

		TerrainLayer TerrainLayer::FromFolder(const std::string& folder) {
			auto file = [&](const char* name) {
				const std::string path = folder + "/" + name;
				return std::filesystem::exists(path) ? path : std::string();
			};
			TerrainLayer layer;
			layer.name = std::filesystem::path(folder).filename().string();
			layer.albedo = file("albedo.png");
			layer.normal = file("normal.png");
			layer.roughness = file("roughness.png");
			layer.metallic = file("metallic.png");
			layer.occlusion = file("ao.png");
			layer.height = file("displacement.png");
			return layer;
		}

		std::array<TerrainLayer, TERRAIN_LAYERS> TerrainSettings::DefaultLayers() {
			std::array<TerrainLayer, TERRAIN_LAYERS> layers;
			const char* folders[TERRAIN_LAYERS] = { "grasss", "rocky", "badland", "bumpy" };
			const char* names[TERRAIN_LAYERS] = { "Grass", "Rock", "Badland", "Bumpy ground" };
			const float tiling[TERRAIN_LAYERS] = { 2.5f, 4.0f, 4.0f, 3.0f };
			for (int i = 0; i < TERRAIN_LAYERS; i++) {
				layers[i] = TerrainLayer::FromFolder(std::string("Textures/terrain/") + folders[i]);
				layers[i].name = names[i];
				layers[i].tiling = tiling[i];
			}
			return layers;
		}

		// --- Grid ---

		void Terrain::Create(const TerrainSettings& newSettings) {
			settings = newSettings;
			settings.resolution = std::clamp(settings.resolution, 2, 4097);
			settings.blendResolution = std::clamp(settings.blendResolution, 2, 4096);
			heights.assign(static_cast<size_t>(settings.resolution) * settings.resolution, 0.0f);
			blend.assign(static_cast<size_t>(settings.blendResolution) * settings.blendResolution, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
			boundsValid = false;
			MarkAllDirty();
		}

		void Terrain::Resize(int resolution, int blendResolution) {
			resolution = std::clamp(resolution, 2, 4097);
			blendResolution = std::clamp(blendResolution, 2, 4096);
			if (resolution != settings.resolution) {
				heights = ResampleCorners(heights, settings.resolution, resolution);
				settings.resolution = resolution;
			}
			if (blendResolution != settings.blendResolution) {
				blend = ResampleCorners(blend, settings.blendResolution, blendResolution);
				settings.blendResolution = blendResolution;
			}
			boundsValid = false;
			MarkAllDirty();
		}

		float Terrain::Height(int column, int row) const {
			const int n = settings.resolution;
			column = std::clamp(column, 0, n - 1);
			row = std::clamp(row, 0, n - 1);
			return heights[static_cast<size_t>(row) * n + column];
		}

		float Terrain::LocalHeight(float x, float z) const {
			const int n = settings.resolution;
			const float half = settings.size * 0.5f;
			const float fx = glm::clamp((x + half) / Spacing(), 0.0f, static_cast<float>(n - 1));
			const float fz = glm::clamp((z + half) / Spacing(), 0.0f, static_cast<float>(n - 1));
			const int c = std::min(static_cast<int>(fx), n - 2), r = std::min(static_cast<int>(fz), n - 2);
			const float tx = fx - c, tz = fz - r;
			return glm::mix(glm::mix(Height(c, r), Height(c + 1, r), tx), glm::mix(Height(c, r + 1), Height(c + 1, r + 1), tx), tz);
		}

		bool Terrain::HeightAt(float x, float z, float& y) const {
			if (!IsCreated()) {
				return false;
			}
			const float half = settings.size * 0.5f;
			const float lx = x - settings.position.x, lz = z - settings.position.z;
			if (std::abs(lx) > half || std::abs(lz) > half) {
				return false;
			}
			y = settings.position.y + LocalHeight(lx, lz);
			return true;
		}

		void Terrain::HeightBounds() const {
			if (boundsValid) {
				return;
			}
			const auto [low, high] = std::minmax_element(heights.begin(), heights.end());
			minHeight = *low;
			maxHeight = *high;
			boundsValid = true;
		}

		bool Terrain::Raycast(const glm::vec3& worldOrigin, const glm::vec3& direction, glm::vec3& hit) const {
			if (!IsCreated() || glm::dot(direction, direction) < 1e-12f) {
				return false;
			}
			HeightBounds();
			const glm::vec3 origin = worldOrigin - settings.position;
			const glm::vec3 d = glm::normalize(direction);
			const float half = settings.size * 0.5f;

			// The part of the ray inside the terrain's bounding box
			const glm::vec3 boxMin(-half, minHeight - 0.01f, -half), boxMax(half, maxHeight + 0.01f, half);
			float tNear = 0.0f, tFar = 1e6f;
			for (int axis = 0; axis < 3; axis++) {
				if (std::abs(d[axis]) < 1e-9f) {
					if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis]) {
						return false;
					}
					continue;
				}
				float t0 = (boxMin[axis] - origin[axis]) / d[axis];
				float t1 = (boxMax[axis] - origin[axis]) / d[axis];
				if (t0 > t1) std::swap(t0, t1);
				tNear = std::max(tNear, t0);
				tFar = std::min(tFar, t1);
			}
			if (tNear > tFar) {
				return false;
			}

			// March in steps of half a grid cell until the ray is below the surface, then bisect
			auto above = [&](float t) {
				const glm::vec3 p = origin + d * t;
				return p.y - LocalHeight(p.x, p.z);
			};
			const float step = Spacing() * 0.5f;
			float previousT = tNear;
			float previous = above(tNear);
			if (previous < 0.0f) {
				return false; // Starts below the ground
			}
			for (float t = tNear + step; t <= tFar + step; t += step) {
				const float current = above(std::min(t, tFar));
				if (current <= 0.0f) {
					float a = previousT, b = std::min(t, tFar);
					for (int i = 0; i < 16; i++) {
						const float m = 0.5f * (a + b);
						(above(m) > 0.0f ? a : b) = m;
					}
					hit = settings.position + origin + d * b;
					return true;
				}
				previousT = std::min(t, tFar);
				previous = current;
			}
			return false;
		}

		// --- Mesh ---

		Fox::Graphics::Vulkan::Vertex Terrain::MakeVertex(int column, int row) const {
			const int n = settings.resolution;
			const float s = Spacing();
			const float half = settings.size * 0.5f;

			Fox::Graphics::Vulkan::Vertex vertex{};
			vertex.position[0] = -half + column * s;
			vertex.position[1] = Height(column, row);
			vertex.position[2] = -half + row * s;
			vertex.position[3] = 1.0f;

			// Central differences (one sided at the edges)
			const float left = Height(column - 1, row), right = Height(column + 1, row);
			const float down = Height(column, row - 1), up = Height(column, row + 1);
			const float dx = s * static_cast<float>(std::min(column + 1, n - 1) - std::max(column - 1, 0));
			const float dz = s * static_cast<float>(std::min(row + 1, n - 1) - std::max(row - 1, 0));
			const glm::vec3 normal = glm::normalize(glm::vec3((left - right) / dx, 1.0f, (down - up) / dz));
			const glm::vec3 tangent = glm::normalize(glm::vec3(1.0f, (right - left) / dx, 0.0f));
			for (int c = 0; c < 3; c++) {
				vertex.normal[c] = normal[c];
				vertex.tangent[c] = tangent[c];
			}
			vertex.tangent[3] = 1.0f;

			// The blend map covers the terrain once; the layer textures are projected from the position instead
			vertex.uv[0] = static_cast<float>(column) / (n - 1);
			vertex.uv[1] = static_cast<float>(row) / (n - 1);
			return vertex;
		}

		void Terrain::BuildRows(int firstRow, int lastRow, std::vector<Fox::Graphics::Vulkan::Vertex>& vertices) const {
			const int n = settings.resolution;
			vertices.clear();
			vertices.reserve(static_cast<size_t>(lastRow - firstRow + 1) * n);
			for (int row = firstRow; row <= lastRow; row++) {
				for (int column = 0; column < n; column++) {
					vertices.push_back(MakeVertex(column, row));
				}
			}
		}

		void Terrain::BuildMesh(std::vector<Fox::Graphics::Vulkan::Vertex>& vertices, std::vector<uint32_t>& indices) const {
			const int n = settings.resolution;
			BuildRows(0, n - 1, vertices);
			indices.clear();
			indices.reserve(static_cast<size_t>(n - 1) * (n - 1) * 6);
			for (int row = 0; row < n - 1; row++) {
				for (int column = 0; column < n - 1; column++) {
					const uint32_t a = static_cast<uint32_t>(row * n + column); // (x, z)
					const uint32_t b = a + 1;                                    // (x + 1, z)
					const uint32_t c = a + n;                                    // (x, z + 1)
					const uint32_t d = c + 1;                                    // (x + 1, z + 1)
					// Counter-clockwise from above. The diagonal alternates, so slopes look the same in every direction.
					if ((row + column) % 2 == 0) {
						indices.insert(indices.end(), { a, c, b, b, c, d });
					}
					else {
						indices.insert(indices.end(), { a, c, d, a, d, b });
					}
				}
			}
		}

		// --- Brushes ---

		void Terrain::BeginStroke(const glm::vec3& point) {
			strokeHeight = point.y - settings.position.y;
		}

		void Terrain::ApplyBrush(const TerrainBrush& brush, const glm::vec3& point, float deltaTime) {
			if (!IsCreated() || brush.radius <= 0.0f || deltaTime <= 0.0f) {
				return;
			}
			const float half = settings.size * 0.5f;
			const glm::vec2 center(point.x - settings.position.x, point.z - settings.position.z);

			if (brush.mode == TerrainBrushMode::Paint) {
				const int m = settings.blendResolution;
				const float texel = settings.size / m;
				const int x0 = std::max(static_cast<int>(std::floor((center.x - brush.radius + half) / texel)), 0);
				const int x1 = std::min(static_cast<int>(std::ceil((center.x + brush.radius + half) / texel)), m - 1);
				const int y0 = std::max(static_cast<int>(std::floor((center.y - brush.radius + half) / texel)), 0);
				const int y1 = std::min(static_cast<int>(std::ceil((center.y + brush.radius + half) / texel)), m - 1);
				glm::vec4 target(0.0f);
				target[std::clamp(brush.layer, 0, TERRAIN_LAYERS - 1)] = 1.0f;
				bool changed = false;
				for (int y = y0; y <= y1; y++) {
					for (int x = x0; x <= x1; x++) {
						const glm::vec2 p(-half + (x + 0.5f) * texel, -half + (y + 0.5f) * texel);
						const float d = glm::length(p - center) / brush.radius;
						if (d >= 1.0f) {
							continue;
						}
						// Towards the layer, the others fading in proportion: the weights keep summing to 1
						const float amount = 1.0f - std::exp(-brush.strength * deltaTime * Falloff(d, brush.hardness));
						glm::vec4& weights = blend[static_cast<size_t>(y) * m + x];
						weights = glm::mix(weights, target, amount);
						changed = true;
					}
				}
				if (changed) {
					dirtyX0 = std::min(dirtyX0, x0);
					dirtyY0 = std::min(dirtyY0, y0);
					dirtyX1 = std::max(dirtyX1, x1 + 1);
					dirtyY1 = std::max(dirtyY1, y1 + 1);
				}
				return;
			}

			const int n = settings.resolution;
			const float s = Spacing();
			const int c0 = std::max(static_cast<int>(std::floor((center.x - brush.radius + half) / s)), 0);
			const int c1 = std::min(static_cast<int>(std::ceil((center.x + brush.radius + half) / s)), n - 1);
			const int r0 = std::max(static_cast<int>(std::floor((center.y - brush.radius + half) / s)), 0);
			const int r1 = std::min(static_cast<int>(std::ceil((center.y + brush.radius + half) / s)), n - 1);
			if (c0 > c1 || r0 > r1) {
				return;
			}

			// Smoothing averages the heights as they were before this step
			std::vector<float> before;
			if (brush.mode == TerrainBrushMode::Smooth) {
				before.reserve(static_cast<size_t>(c1 - c0 + 3) * (r1 - r0 + 3));
				for (int r = r0 - 1; r <= r1 + 1; r++) {
					for (int c = c0 - 1; c <= c1 + 1; c++) {
						before.push_back(Height(c, r));
					}
				}
			}
			const int width = c1 - c0 + 3;
			auto previous = [&](int c, int r) { return before[static_cast<size_t>(r - r0 + 1) * width + (c - c0 + 1)]; };

			bool changed = false;
			for (int r = r0; r <= r1; r++) {
				for (int c = c0; c <= c1; c++) {
					const glm::vec2 p(-half + c * s, -half + r * s);
					const float d = glm::length(p - center) / brush.radius;
					if (d >= 1.0f) {
						continue;
					}
					const float w = Falloff(d, brush.hardness);
					float& h = heights[static_cast<size_t>(r) * n + c];
					switch (brush.mode) {
					case TerrainBrushMode::Raise:
						h += brush.strength * deltaTime * w;
						break;
					case TerrainBrushMode::Lower:
						h -= brush.strength * deltaTime * w;
						break;
					case TerrainBrushMode::Smooth: {
						float sum = 0.0f;
						for (int j = -1; j <= 1; j++) {
							for (int i = -1; i <= 1; i++) {
								sum += previous(c + i, r + j);
							}
						}
						h = glm::mix(h, sum / 9.0f, 1.0f - std::exp(-4.0f * brush.strength * deltaTime * w));
						break;
					}
					case TerrainBrushMode::Flatten:
						h = glm::mix(h, strokeHeight, 1.0f - std::exp(-brush.strength * deltaTime * w));
						break;
					default:
						break;
					}
					changed = true;
				}
			}
			if (changed) {
				// Normals of the neighbouring rows change too
				dirtyRow0 = std::min(dirtyRow0, std::max(r0 - 1, 0));
				dirtyRow1 = std::max(dirtyRow1, std::min(r1 + 1, n - 1));
				boundsValid = false;
			}
		}

		// --- Changes ---

		void Terrain::MarkAllDirty() {
			dirtyRow0 = 0;
			dirtyRow1 = settings.resolution - 1;
			dirtyX0 = 0;
			dirtyY0 = 0;
			dirtyX1 = settings.blendResolution;
			dirtyY1 = settings.blendResolution;
		}

		bool Terrain::TakeDirtyRows(int& firstRow, int& lastRow) {
			if (dirtyRow0 > dirtyRow1) {
				return false;
			}
			firstRow = dirtyRow0;
			lastRow = dirtyRow1;
			dirtyRow0 = INT32_MAX;
			dirtyRow1 = -1;
			return true;
		}

		bool Terrain::TakeDirtyBlend(int& x0, int& y0, int& x1, int& y1) {
			if (dirtyX0 >= dirtyX1 || dirtyY0 >= dirtyY1) {
				return false;
			}
			x0 = dirtyX0;
			y0 = dirtyY0;
			x1 = dirtyX1;
			y1 = dirtyY1;
			dirtyX0 = dirtyY0 = INT32_MAX;
			dirtyX1 = dirtyY1 = -1;
			return true;
		}

		std::vector<uint8_t> Terrain::BlendPixels(int x0, int y0, int x1, int y1) const {
			const int m = settings.blendResolution;
			std::vector<uint8_t> pixels(static_cast<size_t>(x1 - x0) * (y1 - y0) * 4);
			size_t at = 0;
			for (int y = y0; y < y1; y++) {
				for (int x = x0; x < x1; x++) {
					const glm::vec4& weights = blend[static_cast<size_t>(y) * m + x];
					for (int c = 0; c < 4; c++) {
						pixels[at++] = static_cast<uint8_t>(std::lround(glm::clamp(weights[c], 0.0f, 1.0f) * 255.0f));
					}
				}
			}
			return pixels;
		}

		// --- Files ---

		bool Terrain::SaveHeights(const std::string& path, std::string& error) const {
			std::ofstream out(path, std::ios::binary);
			if (!out) {
				error = "Cannot write " + path;
				return false;
			}
			const int32_t resolution = settings.resolution;
			out.write(reinterpret_cast<const char*>(&HEIGHTS_MAGIC), sizeof(HEIGHTS_MAGIC));
			out.write(reinterpret_cast<const char*>(&resolution), sizeof(resolution));
			out.write(reinterpret_cast<const char*>(heights.data()), static_cast<std::streamsize>(heights.size() * sizeof(float)));
			return static_cast<bool>(out);
		}

		bool Terrain::LoadHeights(const std::string& path, std::string& error) {
			std::ifstream in(path, std::ios::binary);
			uint32_t magic = 0;
			int32_t resolution = 0;
			in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
			in.read(reinterpret_cast<char*>(&resolution), sizeof(resolution));
			if (!in || magic != HEIGHTS_MAGIC || resolution < 2 || resolution > 4097) {
				error = "Not a terrain height file: " + path;
				return false;
			}
			std::vector<float> values(static_cast<size_t>(resolution) * resolution);
			in.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(float)));
			if (!in) {
				error = "Truncated terrain height file: " + path;
				return false;
			}
			heights = std::move(values);
			settings.resolution = resolution;
			boundsValid = false;
			MarkAllDirty();
			return true;
		}

		bool Terrain::SaveBlend(const std::string& path, std::string& error) const {
			const int m = settings.blendResolution;
			const std::vector<uint8_t> pixels = BlendPixels(0, 0, m, m);
			if (!stbi_write_png(path.c_str(), m, m, 4, pixels.data(), m * 4)) {
				error = "Cannot write " + path;
				return false;
			}
			return true;
		}

		bool Terrain::LoadBlend(const std::string& path, std::string& error) {
			int width = 0, height = 0, channels = 0;
			stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
			if (!pixels) {
				error = "Cannot read " + path;
				return false;
			}
			if (width != height || width < 2) {
				stbi_image_free(pixels);
				error = "The blend map must be square: " + path;
				return false;
			}
			blend.resize(static_cast<size_t>(width) * height);
			for (size_t i = 0; i < blend.size(); i++) {
				glm::vec4 weights(pixels[i * 4], pixels[i * 4 + 1], pixels[i * 4 + 2], pixels[i * 4 + 3]);
				const float sum = weights.x + weights.y + weights.z + weights.w;
				blend[i] = sum > 0.0f ? weights / sum : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
			}
			stbi_image_free(pixels);
			settings.blendResolution = width;
			MarkAllDirty();
			return true;
		}
	}
}
