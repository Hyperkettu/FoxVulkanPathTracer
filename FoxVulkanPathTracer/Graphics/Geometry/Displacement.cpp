#include "FoxRenderer.h"

#include <complex>
#include <numeric>

namespace Fox {

	namespace Graphics {

		namespace Geometry {

			namespace {

				using Fox::Core::Loaders::GLTF::VertexData;
				using Fox::Core::Loaders::GLTF::SubmeshData;
				using Fox::Core::Loaders::GLTF::MaterialData;
				using Fox::Core::Loaders::GLTF::TextureData;

				const float PI = 3.14159265358979f;

				// Height fields reconstructed from normal maps are computed at this resolution at most.
				// The tessellation cannot resolve more detail than that anyway.
				const int MAX_DERIVED_RESOLUTION = 512;

				const int MAX_SUBDIVISION_DEPTH = 16;

				// ----------------------------------------------------------------------------------
				// Height fields
				// ----------------------------------------------------------------------------------

				struct HeightField {
					int width = 0;
					int height = 0;
					std::vector<float> values;

					// true:  values are heights in model units, ready to use
					// false: values are in units of one texel's width (reconstructed from a normal map)
					bool inModelUnits = false;
					float maxAbsolute = 0.0f;

					// Mip chain: each level halves the resolution by averaging 2x2 texels. Vertices that are
					// further apart than a texel must sample a coarser level, or the relief aliases into noise
					// (exactly like a texture without mipmaps). Level 0 is `values`.
					struct Level {
						int width;
						int height;
						std::vector<float> values;
					};
					std::vector<Level> mips;

					bool IsValid() const { return width > 0 && height > 0; }

					// Trilinear lookup; the field repeats like a tiling texture.
					// lod: 0 = full resolution, each +1 halves it.
					float Sample(float u, float v, float lod = 0.0f) const {
						const int maxLevel = static_cast<int>(mips.size());
						lod = std::clamp(lod, 0.0f, static_cast<float>(maxLevel));

						const int level = std::min(static_cast<int>(lod), maxLevel);
						const float blend = lod - level;

						const float fine = SampleLevel(level, u, v);
						if (blend <= 0.0f || level >= maxLevel) {
							return fine;
						}
						return fine + (SampleLevel(level + 1, u, v) - fine) * blend;
					}

					void BuildMips() {
						mips.clear();

						int levelWidth = width, levelHeight = height;
						const std::vector<float>* source = &values;

						while (levelWidth > 1 || levelHeight > 1) {
							const int sourceWidth = levelWidth, sourceHeight = levelHeight;
							levelWidth = std::max(1, levelWidth / 2);
							levelHeight = std::max(1, levelHeight / 2);

							Level next{ levelWidth, levelHeight, std::vector<float>(static_cast<size_t>(levelWidth) * levelHeight) };
							for (int y = 0; y < levelHeight; y++) {
								for (int x = 0; x < levelWidth; x++) {
									float sum = 0.0f;
									for (int dy = 0; dy < 2; dy++) {
										for (int dx = 0; dx < 2; dx++) {
											const int sx = std::min(x * 2 + dx, sourceWidth - 1);
											const int sy = std::min(y * 2 + dy, sourceHeight - 1);
											sum += (*source)[static_cast<size_t>(sy) * sourceWidth + sx];
										}
									}
									next.values[static_cast<size_t>(y) * levelWidth + x] = sum * 0.25f;
								}
							}

							mips.push_back(std::move(next));
							source = &mips.back().values;
						}

						levelRms.assign(1, rms);
						for (const Level& level : mips) {
							levelRms.push_back(DeviationOf(level.values));
						}
					}

					// Depth of the relief that is left after filtering to the given mip level
					float RmsAt(float lod) const {
						if (levelRms.empty()) {
							return rms;
						}
						lod = std::clamp(lod, 0.0f, static_cast<float>(levelRms.size() - 1));
						const int level = static_cast<int>(lod);
						const float blend = lod - level;
						const int nextLevel = std::min(level + 1, static_cast<int>(levelRms.size() - 1));
						return levelRms[level] + (levelRms[nextLevel] - levelRms[level]) * blend;
					}

					// Typical depth of the relief: standard deviation of the heights (per mip level in levelRms)
					float rms = 0.0f;
					std::vector<float> levelRms;

					void UpdateMaxAbsolute() {
						maxAbsolute = 0.0f;
						for (float value : values) {
							maxAbsolute = std::max(maxAbsolute, std::abs(value));
						}
						rms = DeviationOf(values);
					}

					// Around the mean: a height map's midlevel does not have to be its average
					static float DeviationOf(const std::vector<float>& data) {
						if (data.empty()) {
							return 0.0f;
						}
						double sum = 0.0, sumOfSquares = 0.0;
						for (float value : data) {
							sum += value;
							sumOfSquares += static_cast<double>(value) * value;
						}
						const double mean = sum / data.size();
						return static_cast<float>(std::sqrt(std::max(sumOfSquares / data.size() - mean * mean, 0.0)));
					}

				private:
					float SampleLevel(int level, float u, float v) const {
						const int levelWidth = level == 0 ? width : mips[level - 1].width;
						const int levelHeight = level == 0 ? height : mips[level - 1].height;
						const std::vector<float>& levelValues = level == 0 ? values : mips[level - 1].values;

						const float x = (u - std::floor(u)) * levelWidth - 0.5f;
						const float y = (v - std::floor(v)) * levelHeight - 0.5f;

						const int x0 = static_cast<int>(std::floor(x));
						const int y0 = static_cast<int>(std::floor(y));
						const float fx = x - x0;
						const float fy = y - y0;

						auto at = [&](int px, int py) {
							px = ((px % levelWidth) + levelWidth) % levelWidth;
							py = ((py % levelHeight) + levelHeight) % levelHeight;
							return levelValues[static_cast<size_t>(py) * levelWidth + px];
						};

						const float top = at(x0, y0) * (1.0f - fx) + at(x0 + 1, y0) * fx;
						const float bottom = at(x0, y0 + 1) * (1.0f - fx) + at(x0 + 1, y0 + 1) * fx;
						return top * (1.0f - fy) + bottom * fy;
					}
				};

				// In-place radix-2 FFT of n values that are `stride` apart. n must be a power of two.
				void FFT(std::complex<float>* data, int n, int stride, bool inverse) {
					// Bit reversal permutation
					for (int i = 1, j = 0; i < n; i++) {
						int bit = n >> 1;
						for (; j & bit; bit >>= 1) {
							j ^= bit;
						}
						j ^= bit;

						if (i < j) {
							std::swap(data[i * stride], data[j * stride]);
						}
					}

					for (int length = 2; length <= n; length <<= 1) {
						const float angle = 2.0f * PI / length * (inverse ? 1.0f : -1.0f);
						const std::complex<float> step(std::cos(angle), std::sin(angle));

						for (int start = 0; start < n; start += length) {
							std::complex<float> w(1.0f, 0.0f);
							for (int k = 0; k < length / 2; k++) {
								std::complex<float>& a = data[(start + k) * stride];
								std::complex<float>& b = data[(start + k + length / 2) * stride];
								const std::complex<float> t = b * w;
								b = a - t;
								a = a + t;
								w *= step;
							}
						}
					}

					if (inverse) {
						for (int i = 0; i < n; i++) {
							data[i * stride] /= static_cast<float>(n);
						}
					}
				}

				void FFT2D(std::vector<std::complex<float>>& data, int width, int height, bool inverse) {
					for (int y = 0; y < height; y++) {
						FFT(data.data() + static_cast<size_t>(y) * width, width, 1, inverse);
					}
					for (int x = 0; x < width; x++) {
						FFT(data.data() + x, height, width, inverse);
					}
				}

				int FloorPowerOfTwo(int value) {
					int result = 1;
					while (result * 2 <= value) {
						result *= 2;
					}
					return result;
				}

				// Angular frequency of FFT bin k out of n. The Nyquist bin has no defined sign: drop it.
				float AngularFrequency(int k, int n) {
					if (k == n / 2) {
						return 0.0f;
					}
					const int signedIndex = k < n / 2 ? k : k - n;
					return 2.0f * PI * signedIndex / n;
				}

				// Reconstructs the relief a tangent space normal map describes.
				//
				// A normal map stores the slope of the surface in every texel. The height field with those
				// slopes is found by integrating them, done here in the frequency domain (Frankot & Chellappa,
				// 1988), which gives the least squares solution and suits tiling textures because the Fourier
				// transform assumes the image repeats.
				HeightField DeriveFromNormalMap(const TextureData& texture, float normalScale) {
					HeightField field;

					if (texture.width < 4 || texture.height < 4 || texture.componentCount < 3) {
						return field;
					}

					const int width = std::min(MAX_DERIVED_RESOLUTION, FloorPowerOfTwo(texture.width));
					const int height = std::min(MAX_DERIVED_RESOLUTION, FloorPowerOfTwo(texture.height));
					const size_t count = static_cast<size_t>(width) * height;

					// Slopes along the image axes, from normals averaged over the source texels of each cell
					std::vector<float> slopeX(count), slopeY(count);

					for (int y = 0; y < height; y++) {
						const int sourceY0 = y * texture.height / height;
						const int sourceY1 = std::max(sourceY0 + 1, (y + 1) * texture.height / height);

						for (int x = 0; x < width; x++) {
							const int sourceX0 = x * texture.width / width;
							const int sourceX1 = std::max(sourceX0 + 1, (x + 1) * texture.width / width);

							glm::vec3 normal(0.0f);
							for (int sy = sourceY0; sy < sourceY1; sy++) {
								for (int sx = sourceX0; sx < sourceX1; sx++) {
									const unsigned char* pixel = texture.pixelData.data() + (static_cast<size_t>(sy) * texture.width + sx) * texture.componentCount;
									normal += glm::vec3(pixel[0], pixel[1], pixel[2]) / 255.0f * 2.0f - 1.0f;
								}
							}

							normal.x *= normalScale;
							normal.y *= normalScale;
							normal.z = std::max(normal.z, 0.1f * glm::length(normal)); // Keep the slope finite

							// A surface z = h(x, y) has the normal (-dh/dx, -dh/dy, 1)
							slopeX[static_cast<size_t>(y) * width + x] = -normal.x / normal.z;
							slopeY[static_cast<size_t>(y) * width + x] = -normal.y / normal.z;
						}
					}

					// Normal maps come in two conventions (green pointing up or down the image). Only one of
					// them describes a surface that can exist: its slope field has (almost) no curl.
					auto wrapped = [&](const std::vector<float>& values, int x, int y) {
						return values[static_cast<size_t>((y + height) % height) * width + (x + width) % width];
					};

					double curlAsIs = 0.0, curlFlipped = 0.0;
					for (int y = 0; y < height; y++) {
						for (int x = 0; x < width; x++) {
							const float dSlopeXdY = wrapped(slopeX, x, y + 1) - wrapped(slopeX, x, y - 1);
							const float dSlopeYdX = wrapped(slopeY, x + 1, y) - wrapped(slopeY, x - 1, y);
							curlAsIs += std::pow(dSlopeXdY - dSlopeYdX, 2.0);
							curlFlipped += std::pow(dSlopeXdY + dSlopeYdX, 2.0);
						}
					}

					const float ySign = curlFlipped < curlAsIs ? -1.0f : 1.0f;

					std::vector<std::complex<float>> spectrumX(count), spectrumY(count);
					for (size_t i = 0; i < count; i++) {
						spectrumX[i] = slopeX[i];
						spectrumY[i] = slopeY[i] * ySign;
					}

					FFT2D(spectrumX, width, height, false);
					FFT2D(spectrumY, width, height, false);

					// d/dx becomes a multiplication with i*wx: solve i*wx*H = P and i*wy*H = Q for H
					const std::complex<float> i(0.0f, 1.0f);
					for (int y = 0; y < height; y++) {
						const float wy = AngularFrequency(y, height);
						for (int x = 0; x < width; x++) {
							const float wx = AngularFrequency(x, width);
							const float denominator = wx * wx + wy * wy;
							const size_t index = static_cast<size_t>(y) * width + x;

							spectrumX[index] = denominator > 0.0f
								? (-i * wx * spectrumX[index] - i * wy * spectrumY[index]) / denominator
								: std::complex<float>(0.0f); // The mean height is arbitrary: zero
						}
					}

					FFT2D(spectrumX, width, height, true);

					field.width = width;
					field.height = height;
					field.values.resize(count);
					for (size_t index = 0; index < count; index++) {
						field.values[index] = spectrumX[index].real();
					}
					field.inModelUnits = false;
					field.UpdateMaxAbsolute();
					field.BuildMips();

					return field;
				}

				HeightField LoadHeightMap(const DisplacementSettings::HeightMap& heightMap) {
					HeightField field;

					int width = 0, height = 0, channels = 0;
					stbi_us* pixels = stbi_load_16(heightMap.file.c_str(), &width, &height, &channels, 1);
					if (!pixels) {
						std::cerr << "Displacement: could not load height map " << heightMap.file << "\n";
						return field;
					}

					field.width = width;
					field.height = height;
					field.values.resize(static_cast<size_t>(width) * height);
					for (size_t index = 0; index < field.values.size(); index++) {
						field.values[index] = (pixels[index] / 65535.0f - heightMap.midlevel) * heightMap.scale;
					}
					field.inModelUnits = true;
					field.UpdateMaxAbsolute();
					field.BuildMips();

					stbi_image_free(pixels);
					return field;
				}

				// ----------------------------------------------------------------------------------
				// Tessellation
				// ----------------------------------------------------------------------------------

				glm::vec3 Position(const VertexData& vertex) {
					return glm::vec3(vertex.position[0], vertex.position[1], vertex.position[2]);
				}

				// Same transform as TransformUV in Shaders/closest_hit.slang
				glm::vec2 TransformUV(const VertexData& vertex, const MaterialData& material) {
					glm::vec2 uv(vertex.texCoord[0] * material.uvScale[0], vertex.texCoord[1] * material.uvScale[1]);

					if (material.uvRotation != 0.0f) {
						const float s = std::sin(material.uvRotation);
						const float c = std::cos(material.uvRotation);
						uv = glm::vec2(c * uv.x + s * uv.y, -s * uv.x + c * uv.y);
					}

					return uv;
				}

				// Splits triangles until no edge is longer than the target length.
				//
				// Whether an edge is split depends only on the edge itself, never on the triangle it belongs
				// to. Two triangles sharing an edge therefore always agree on it, which keeps the surface
				// watertight - also across UV seams and hard edges, where vertices are duplicated.
				class Tessellator {
				public:
					// expectedTriangles: the output size if known, so the containers are allocated once
					Tessellator(std::vector<VertexData>& vertices, std::vector<uint32_t>& indices, float maxEdgeLength, size_t expectedTriangles = 0)
						: vertices(vertices), indices(indices), maxEdgeLengthSquared(maxEdgeLength * maxEdgeLength) {
						// A tessellated mesh has about half as many vertices as triangles, most of them new midpoints
						indices.reserve(indices.size() + expectedTriangles * 3);
						vertices.reserve(vertices.size() + expectedTriangles / 2 + 16);
						midpoints.reserve(expectedTriangles / 2 + 16);
					}

					void AddTriangle(uint32_t a, uint32_t b, uint32_t c, int depth = 0) {
						const bool splitAB = NeedsSplit(a, b, depth);
						const bool splitBC = NeedsSplit(b, c, depth);
						const bool splitCA = NeedsSplit(c, a, depth);

						const int splits = (splitAB ? 1 : 0) + (splitBC ? 1 : 0) + (splitCA ? 1 : 0);

						if (splits == 0) {
							indices.push_back(a);
							indices.push_back(b);
							indices.push_back(c);
							return;
						}

						// Rotate the corners so the cases below only need one orientation each
						if (splits == 1) {
							if (splitBC) { Rotate(a, b, c); }
							else if (splitCA) { Rotate(a, b, c); Rotate(a, b, c); }

							// Only AB is split
							const uint32_t ab = Midpoint(a, b);
							AddTriangle(a, ab, c, depth + 1);
							AddTriangle(ab, b, c, depth + 1);
						}
						else if (splits == 2) {
							if (!splitAB) { Rotate(a, b, c); }
							else if (!splitBC) { Rotate(a, b, c); Rotate(a, b, c); }

							// AB and BC are split, CA is not
							const uint32_t ab = Midpoint(a, b);
							const uint32_t bc = Midpoint(b, c);
							AddTriangle(ab, b, bc, depth + 1);
							AddTriangle(a, ab, bc, depth + 1);
							AddTriangle(a, bc, c, depth + 1);
						}
						else {
							const uint32_t ab = Midpoint(a, b);
							const uint32_t bc = Midpoint(b, c);
							const uint32_t ca = Midpoint(c, a);
							AddTriangle(a, ab, ca, depth + 1);
							AddTriangle(ab, b, bc, depth + 1);
							AddTriangle(ca, bc, c, depth + 1);
							AddTriangle(ab, bc, ca, depth + 1);
						}
					}

				private:
					// (a, b, c) -> (b, c, a): same triangle, same winding
					static void Rotate(uint32_t& a, uint32_t& b, uint32_t& c) {
						const uint32_t first = a;
						a = b;
						b = c;
						c = first;
					}

					bool NeedsSplit(uint32_t a, uint32_t b, int depth) const {
						if (depth >= MAX_SUBDIVISION_DEPTH) {
							return false;
						}
						const glm::vec3 edge = Position(vertices[a]) - Position(vertices[b]);
						return glm::dot(edge, edge) > maxEdgeLengthSquared;
					}

					uint32_t Midpoint(uint32_t a, uint32_t b) {
						const uint64_t key = (static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b);

						auto existing = midpoints.find(key);
						if (existing != midpoints.end()) {
							return existing->second;
						}

						// Copies, not references: push_back below may reallocate
						const VertexData va = vertices[a];
						const VertexData vb = vertices[b];

						VertexData middle{};
						for (int i = 0; i < 3; i++) {
							// (a + b) is the same for both orders, so duplicated edges get identical midpoints
							middle.position[i] = (va.position[i] + vb.position[i]) * 0.5f;
							middle.normal[i] = (va.normal[i] + vb.normal[i]) * 0.5f;
							middle.tangent[i] = (va.tangent[i] + vb.tangent[i]) * 0.5f;
						}
						middle.texCoord[0] = (va.texCoord[0] + vb.texCoord[0]) * 0.5f;
						middle.texCoord[1] = (va.texCoord[1] + vb.texCoord[1]) * 0.5f;
						middle.tangent[3] = va.tangent[3];

						const float normalLength = std::sqrt(middle.normal[0] * middle.normal[0] + middle.normal[1] * middle.normal[1] + middle.normal[2] * middle.normal[2]);
						if (normalLength > 1e-8f) {
							for (int i = 0; i < 3; i++) {
								middle.normal[i] /= normalLength;
							}
						}

						const uint32_t index = static_cast<uint32_t>(vertices.size());
						vertices.push_back(middle);
						midpoints.emplace(key, index);
						return index;
					}

					std::vector<VertexData>& vertices;
					std::vector<uint32_t>& indices;
					float maxEdgeLengthSquared;
					std::unordered_map<uint64_t, uint32_t> midpoints;
				};

				// Counts the triangles Tessellator::AddTriangle would produce, with the same splitting rule and
				// the same midpoint arithmetic. Adds to count; returns false as soon as count exceeds budget.
				bool CountTessellatedTriangles(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, int depth, float maxEdgeLengthSquared, size_t budget, size_t& count) {
					auto needsSplit = [&](const glm::vec3& p, const glm::vec3& q) {
						if (depth >= MAX_SUBDIVISION_DEPTH) {
							return false;
						}
						const glm::vec3 edge = p - q;
						return glm::dot(edge, edge) > maxEdgeLengthSquared;
					};

					const bool splitAB = needsSplit(a, b);
					const bool splitBC = needsSplit(b, c);
					const bool splitCA = needsSplit(c, a);
					const int splits = (splitAB ? 1 : 0) + (splitBC ? 1 : 0) + (splitCA ? 1 : 0);

					if (splits == 0) {
						return ++count <= budget;
					}

					const glm::vec3 ab = (a + b) * 0.5f;
					const glm::vec3 bc = (b + c) * 0.5f;
					const glm::vec3 ca = (c + a) * 0.5f;
					const int next = depth + 1;

					auto recurse = [&](const glm::vec3& p, const glm::vec3& q, const glm::vec3& r) {
						return CountTessellatedTriangles(p, q, r, next, maxEdgeLengthSquared, budget, count);
					};

					if (splits == 1) {
						if (splitAB) return recurse(a, ab, c) && recurse(ab, b, c);
						if (splitBC) return recurse(b, bc, a) && recurse(bc, c, a);
						return recurse(c, ca, b) && recurse(ca, a, b);
					}

					if (splits == 2) {
						// Same as the Tessellator after rotating the unsplit edge to CA
						if (!splitCA) return recurse(ab, b, bc) && recurse(a, ab, bc) && recurse(a, bc, c);
						if (!splitAB) return recurse(bc, c, ca) && recurse(b, bc, ca) && recurse(b, ca, a);
						return recurse(ca, a, ab) && recurse(c, ca, ab) && recurse(c, ab, b);
					}

					return recurse(a, ab, ca) && recurse(ab, b, bc) && recurse(ca, bc, c) && recurse(ab, bc, ca);
				}

				float TriangleArea(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
					return 0.5f * glm::length(glm::cross(b - a, c - a));
				}

				// Vertices at the same place get the same key (0.1 mm grid)
				uint64_t PositionKey(const VertexData& vertex) {
					auto quantize = [](float value) {
						return static_cast<uint64_t>(std::llround(static_cast<double>(value) * 10000.0)) & 0x1FFFFFull;
					};
					return quantize(vertex.position[0]) | (quantize(vertex.position[1]) << 21) | (quantize(vertex.position[2]) << 42);
				}
			}

			DisplacementStats ApplyDisplacement(
				const DisplacementSettings& settings,
				std::vector<VertexData>& vertices,
				std::vector<uint32_t>& indices,
				std::vector<SubmeshData>& submeshes,
				std::vector<Fox::Graphics::Vulkan::RayTracing::RayTracingInstance>& instances,
				const std::vector<MaterialData>& materials,
				const std::vector<TextureData>& textures)
			{
				DisplacementStats stats;

				if (!settings.enabled) {
					return stats;
				}

				// --- 1. Find a height source for every material ---
				std::unordered_map<int, HeightField> derivedFields;          // By texture index
				std::unordered_map<std::string, HeightField> loadedFields;   // By file name
				std::vector<const HeightField*> materialFields(materials.size(), nullptr);

				for (size_t i = 0; i < materials.size(); i++) {
					const MaterialData& material = materials[i];

					auto assigned = settings.heightMaps.find(material.name);
					if (assigned != settings.heightMaps.end()) {
						auto loaded = loadedFields.find(assigned->second.file);
						if (loaded == loadedFields.end()) {
							loaded = loadedFields.emplace(assigned->second.file, LoadHeightMap(assigned->second)).first;
						}
						if (loaded->second.IsValid()) {
							materialFields[i] = &loaded->second;
						}
					}
					else if (settings.deriveFromNormalMaps && material.normalTextureIndex >= 0 && material.normalTextureIndex < static_cast<int>(textures.size())) {
						auto derived = derivedFields.find(material.normalTextureIndex);
						if (derived == derivedFields.end()) {
							derived = derivedFields.emplace(material.normalTextureIndex,
								DeriveFromNormalMap(textures[material.normalTextureIndex], material.normalScale)).first;
						}
						if (derived->second.IsValid() && derived->second.maxAbsolute > 0.0f) {
							materialFields[i] = &derived->second;
						}
					}

					if (materialFields[i]) {
						stats.displacedMaterials++;
					}
				}

				if (stats.displacedMaterials == 0) {
					return stats;
				}

				auto fieldOf = [&](const SubmeshData& submesh) -> const HeightField* {
					return submesh.materialIndex < materialFields.size() ? materialFields[submesh.materialIndex] : nullptr;
				};

				// --- 2. Measure every displaced submesh: texture scale on the surface and depth of its relief ---
				struct SubmeshInfo {
					float texelSize = 0.0f;   // One texel of the height field on the surface, in model units
					float heightScale = 1.0f; // Field value -> model units
					double area = 0.0;        // Surface area in model units
					float edgeLength = 0.0f;  // Chosen in step 3
					size_t triangles = 0;     // What the chosen edge length produces
				};
				std::vector<SubmeshInfo> submeshInfo(submeshes.size());

				for (size_t submeshIndex = 0; submeshIndex < submeshes.size(); submeshIndex++) {
					const SubmeshData& submesh = submeshes[submeshIndex];
					const HeightField* field = fieldOf(submesh);
					if (!field) {
						continue;
					}

					const MaterialData& material = materials[submesh.materialIndex];

					double surfaceArea = 0.0, uvArea = 0.0;
					for (uint32_t i = 0; i + 2 < submesh.indexCount; i += 3) {
						const VertexData& a = vertices[indices[submesh.indexOffset + i + 0]];
						const VertexData& b = vertices[indices[submesh.indexOffset + i + 1]];
						const VertexData& c = vertices[indices[submesh.indexOffset + i + 2]];

						surfaceArea += TriangleArea(Position(a), Position(b), Position(c));

						const glm::vec2 uvA = TransformUV(a, material);
						const glm::vec2 uvB = TransformUV(b, material);
						const glm::vec2 uvC = TransformUV(c, material);
						uvArea += 0.5 * std::abs((uvB.x - uvA.x) * (uvC.y - uvA.y) - (uvC.x - uvA.x) * (uvB.y - uvA.y));
					}

					SubmeshInfo& info = submeshInfo[submeshIndex];
					info.area = surfaceArea;
					info.texelSize = uvArea > 0.0 ? static_cast<float>(std::sqrt(surfaceArea / uvArea)) / field->width : 0.0f;

					if (!field->inModelUnits) {
						info.heightScale = info.texelSize * settings.derivedStrength;

						// Normal maps are often exaggerated: keep the relief within a believable range
						if (field->maxAbsolute * info.heightScale > settings.derivedMaxHeight) {
							info.heightScale = settings.derivedMaxHeight / field->maxAbsolute;
						}
					}

					stats.trianglesBefore += submesh.indexCount / 3;
				}

				// Mip level whose texels match the spacing of the vertices. Vertices end up about 0.75 edge
				// lengths apart, because splitting stops as soon as an edge is below the target.
				auto lodFor = [](const SubmeshInfo& info, float edgeLength) {
					return info.texelSize > 0.0f ? std::max(0.0f, std::log2(0.75f * edgeLength / info.texelSize)) : 0.0f;
				};

				// --- 3. Distribute the triangle budget ---
				// Every surface is offered a ladder of edge lengths, from 16x the target down to the target.
				// Finer steps cost triangles and buy relief: the depth of the height field that survives
				// filtering at that resolution, times the area it covers. Greedily take the step with the most
				// relief per triangle until the budget is spent. Surfaces whose relief is fine grained (bark)
				// stop early, surfaces with large features (stones) get fine triangles.
				//
				// The triangle counts are exact: a long, thin triangle of length L becomes about 2 L / edge
				// pieces whatever its area, so they cannot be estimated from the area.
				const float targetEdge = std::max(settings.targetEdgeLength, 1e-4f);
				constexpr int STEPS = 9; // Step k uses targetEdge * 2^((STEPS - 1 - k) / 2): k = 0 is the coarsest
				const size_t TOO_MANY = std::numeric_limits<size_t>::max();

				auto stepEdge = [&](int step) {
					return targetEdge * std::pow(2.0f, 0.5f * (STEPS - 1 - step));
				};

				struct Candidate {
					size_t submeshIndex = 0;
					size_t triangles[STEPS] = {};
					double relief[STEPS] = {};
					int step = 0; // Current choice
				};
				std::vector<Candidate> candidates;

				for (size_t submeshIndex = 0; submeshIndex < submeshes.size(); submeshIndex++) {
					const SubmeshData& submesh = submeshes[submeshIndex];
					const HeightField* field = fieldOf(submesh);
					if (!field) {
						continue;
					}

					const SubmeshInfo& info = submeshInfo[submeshIndex];
					Candidate candidate;
					candidate.submeshIndex = submeshIndex;

					bool overBudget = false;
					for (int step = 0; step < STEPS; step++) {
						const float edge = stepEdge(step);
						candidate.relief[step] = info.area * field->RmsAt(lodFor(info, edge)) * info.heightScale;

						if (overBudget) {
							candidate.triangles[step] = TOO_MANY;
							continue;
						}

						size_t count = 0;
						for (uint32_t i = 0; i + 2 < submesh.indexCount && !overBudget; i += 3) {
							const uint32_t* triangle = &indices[submesh.indexOffset + i];
							overBudget = !CountTessellatedTriangles(Position(vertices[triangle[0]]), Position(vertices[triangle[1]]), Position(vertices[triangle[2]]),
								0, edge * edge, settings.maxTriangles, count);
						}
						candidate.triangles[step] = overBudget ? TOO_MANY : count;
					}

					candidates.push_back(candidate);
				}

				size_t totalTriangles = 0;
				for (const Candidate& candidate : candidates) {
					totalTriangles += candidate.triangles[0] == TOO_MANY ? settings.maxTriangles : candidate.triangles[0];
				}

				while (true) {
					Candidate* best = nullptr;
					double bestRatio = 0.0;

					for (Candidate& candidate : candidates) {
						const int next = candidate.step + 1;
						if (next >= STEPS || candidate.triangles[next] == TOO_MANY) {
							continue;
						}

						const size_t extraTriangles = candidate.triangles[next] - candidate.triangles[candidate.step];
						const double extraRelief = candidate.relief[next] - candidate.relief[candidate.step];
						if (totalTriangles + extraTriangles > settings.maxTriangles || extraRelief <= 0.0) {
							continue;
						}

						const double ratio = extraRelief / static_cast<double>(std::max<size_t>(extraTriangles, 1));
						if (ratio > bestRatio) {
							bestRatio = ratio;
							best = &candidate;
						}
					}

					if (!best) {
						break;
					}

					totalTriangles += best->triangles[best->step + 1] - best->triangles[best->step];
					best->step++;
				}

				stats.edgeLength = std::numeric_limits<float>::max();
				for (const Candidate& candidate : candidates) {
					submeshInfo[candidate.submeshIndex].edgeLength = stepEdge(candidate.step);
					submeshInfo[candidate.submeshIndex].triangles = candidate.triangles[candidate.step];
					stats.edgeLength = std::min(stats.edgeLength, stepEdge(candidate.step));
				}

				// --- 4. Rebuild the mesh, tessellating the displaced submeshes ---
				std::vector<VertexData> newVertices;
				std::vector<uint32_t> newIndices;
				newVertices.reserve(vertices.size());
				newIndices.reserve(indices.size());

				struct DisplacedRange {
					uint32_t vertexOffset, vertexCount;
					uint32_t indexOffset, indexCount;
					const HeightField* field;
					const MaterialData* material;
					float heightScale; // Field value -> model units
					float lod;         // Mip level matching the vertex spacing
				};
				std::vector<DisplacedRange> displacedRanges;

				for (size_t submeshIndex = 0; submeshIndex < submeshes.size(); submeshIndex++) {
					SubmeshData& submesh = submeshes[submeshIndex];
					const HeightField* field = fieldOf(submesh);

					const uint32_t newVertexOffset = static_cast<uint32_t>(newVertices.size());
					const uint32_t newIndexOffset = static_cast<uint32_t>(newIndices.size());

					if (!field) {
						// Unchanged: copy and renumber
						newVertices.insert(newVertices.end(), vertices.begin() + submesh.vertexOffset, vertices.begin() + submesh.vertexOffset + submesh.vertexCount);
						for (uint32_t i = 0; i < submesh.indexCount; i++) {
							newIndices.push_back(indices[submesh.indexOffset + i] - submesh.vertexOffset + newVertexOffset);
						}
					}
					else {
						const MaterialData& material = materials[submesh.materialIndex];

						const SubmeshInfo& info = submeshInfo[submeshIndex];
						const float submeshEdgeLength = info.edgeLength;

						std::vector<VertexData> localVertices(vertices.begin() + submesh.vertexOffset, vertices.begin() + submesh.vertexOffset + submesh.vertexCount);
						std::vector<uint32_t> localIndices;

						Tessellator tessellator(localVertices, localIndices, submeshEdgeLength, info.triangles);
						for (uint32_t i = 0; i + 2 < submesh.indexCount; i += 3) {
							tessellator.AddTriangle(
								indices[submesh.indexOffset + i + 0] - submesh.vertexOffset,
								indices[submesh.indexOffset + i + 1] - submesh.vertexOffset,
								indices[submesh.indexOffset + i + 2] - submesh.vertexOffset);
						}

						const float lod = lodFor(info, submeshEdgeLength);
						const float heightScale = info.heightScale;

						newVertices.insert(newVertices.end(), localVertices.begin(), localVertices.end());
						for (uint32_t index : localIndices) {
							newIndices.push_back(index + newVertexOffset);
						}

						displacedRanges.push_back({
							newVertexOffset, static_cast<uint32_t>(localVertices.size()),
							newIndexOffset, static_cast<uint32_t>(localIndices.size()),
							field, &material, heightScale, lod });

						stats.trianglesAfter += localIndices.size() / 3;
					}

					submesh.vertexOffset = newVertexOffset;
					submesh.vertexCount = static_cast<uint32_t>(newVertices.size()) - newVertexOffset;
					submesh.indexOffset = newIndexOffset;
					submesh.indexCount = static_cast<uint32_t>(newIndices.size()) - newIndexOffset;

					if (submeshIndex < instances.size()) {
						instances[submeshIndex].firstIndex = submesh.indexOffset;
						instances[submeshIndex].indexCount = submesh.indexCount;
					}
				}

				// --- 5. Weld: vertices at the same position must move together, or the surface tears open ---
				// Models duplicate vertices along UV seams and hard edges; each copy has its own normal and
				// would sample a different height. All copies of a position form one group.
				const uint32_t NOT_DISPLACED = ~0u;
				std::vector<uint32_t> groupOf(newVertices.size(), NOT_DISPLACED);

				std::vector<std::pair<uint64_t, uint32_t>> keyed; // (position key, vertex index)
				for (const DisplacedRange& range : displacedRanges) {
					for (uint32_t i = 0; i < range.vertexCount; i++) {
						keyed.emplace_back(PositionKey(newVertices[range.vertexOffset + i]), range.vertexOffset + i);
					}
				}
				std::sort(keyed.data(), keyed.data() + keyed.size()); // Raw pointers: much faster in Debug builds

				uint32_t groupCount = 0;
				for (size_t i = 0; i < keyed.size(); i++) {
					if (i > 0 && keyed[i].first != keyed[i - 1].first) {
						groupCount++;
					}
					groupOf[keyed[i].second] = groupCount;
				}
				groupCount = keyed.empty() ? 0 : groupCount + 1;
				keyed.clear();
				keyed.shrink_to_fit();

				// --- 6. Pin the border: where a displaced surface ends (or meets one that is not displaced)
				// its edge must stay in place, otherwise gaps open up. Border edges are the ones that belong
				// to a single triangle once the vertices are welded. ---
				std::vector<uint64_t> edges;
				for (const DisplacedRange& range : displacedRanges) {
					for (uint32_t i = 0; i + 2 < range.indexCount; i += 3) {
						for (int corner = 0; corner < 3; corner++) {
							const uint32_t a = groupOf[newIndices[range.indexOffset + i + corner]];
							const uint32_t b = groupOf[newIndices[range.indexOffset + i + (corner + 1) % 3]];
							if (a != b) {
								edges.push_back((static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b));
							}
						}
					}
				}
				std::sort(edges.data(), edges.data() + edges.size());

				std::vector<uint8_t> isBorder(groupCount, 0);
				for (size_t i = 0; i < edges.size();) {
					size_t end = i + 1;
					while (end < edges.size() && edges[end] == edges[i]) {
						end++;
					}
					if (end - i == 1) {
						isBorder[static_cast<uint32_t>(edges[i] >> 32)] = 1;
						isBorder[static_cast<uint32_t>(edges[i] & 0xFFFFFFFFull)] = 1;
					}
					i = end;
				}
				edges.clear();
				edges.shrink_to_fit();

				// --- 7. Displace: every group moves by the average of what its members ask for ---
				std::vector<glm::vec3> groupOffset(groupCount, glm::vec3(0.0f));
				std::vector<uint32_t> groupSize(groupCount, 0);

				for (const DisplacedRange& range : displacedRanges) {
					for (uint32_t i = 0; i < range.vertexCount; i++) {
						const VertexData& vertex = newVertices[range.vertexOffset + i];
						const uint32_t group = groupOf[range.vertexOffset + i];

						const glm::vec2 uv = TransformUV(vertex, *range.material);
						const float height = range.field->Sample(uv.x, uv.y, range.lod) * range.heightScale;

						const glm::vec3 normal(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
						const float normalLength = glm::length(normal);
						if (normalLength > 1e-8f) {
							groupOffset[group] += normal / normalLength * height;
						}
						groupSize[group]++;
					}
				}

				for (const DisplacedRange& range : displacedRanges) {
					for (uint32_t i = 0; i < range.vertexCount; i++) {
						const uint32_t group = groupOf[range.vertexOffset + i];
						if (isBorder[group] || groupSize[group] == 0) {
							continue;
						}

						const glm::vec3 offset = groupOffset[group] / static_cast<float>(groupSize[group]);
						VertexData& vertex = newVertices[range.vertexOffset + i];
						vertex.position[0] += offset.x;
						vertex.position[1] += offset.y;
						vertex.position[2] += offset.z;
					}
				}

				vertices = std::move(newVertices);
				indices = std::move(newIndices);

				return stats;
			}
		}
	}
}
