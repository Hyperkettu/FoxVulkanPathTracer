#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace RayTracing {

				PFN_vkCreateMicromapEXT OpacityMicromapBuilder::vkCreateMicromapEXT = nullptr;
				PFN_vkDestroyMicromapEXT OpacityMicromapBuilder::vkDestroyMicromapEXT = nullptr;
				PFN_vkCmdBuildMicromapsEXT OpacityMicromapBuilder::vkCmdBuildMicromapsEXT = nullptr;
				PFN_vkGetMicromapBuildSizesEXT OpacityMicromapBuilder::vkGetMicromapBuildSizesEXT = nullptr;
				std::map<std::array<int64_t, 8>, OpacityMicromapBuilder::Classified> OpacityMicromapBuilder::classified;

				namespace {
					// 4-state values
					const uint8_t STATE_TRANSPARENT = 0;
					const uint8_t STATE_OPAQUE = 1;
					const uint8_t STATE_UNKNOWN_OPAQUE = 3; // Undecided: the any-hit shader tests the texture

					const uint32_t MIN_LEVEL = 2;  // Whole bytes per entry
					const uint32_t MAX_LEVEL = 9;  // 262144 micro-triangles, 64 KiB per entry
					const float MICRO_TRIANGLE_TEXELS = 2.0f; // Target edge length of a micro-triangle
					const int SAMPLES = 4;         // Per micro-triangle edge: (SAMPLES + 1)(SAMPLES + 2) / 2 alpha samples

					int64_t Quantize(float value) {
						return static_cast<int64_t>(std::llround(static_cast<double>(value) * 1048576.0));
					}
				}

				float AlphaMask::Sample(float u, float v) const {
					if (width == 0 || height == 0) {
						return 1.0f;
					}
					const float x = std::clamp(u * static_cast<float>(width) - 0.5f, 0.0f, static_cast<float>(width - 1));
					const float y = std::clamp(v * static_cast<float>(height) - 0.5f, 0.0f, static_cast<float>(height - 1));
					const uint32_t x0 = static_cast<uint32_t>(x), y0 = static_cast<uint32_t>(y);
					const uint32_t x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
					const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
					auto at = [&](uint32_t px, uint32_t py) { return static_cast<float>(alpha[static_cast<size_t>(py) * width + px]) / 255.0f; };
					return glm::mix(glm::mix(at(x0, y0), at(x1, y0), fx), glm::mix(at(x0, y1), at(x1, y1), fx), fy);
				}

				// Verbatim from the Vulkan specification (opacity micromap reference code)
				uint32_t OpacityMicromapBuilder::BarycentricsToSpaceFillingCurveIndex(float u, float v, uint32_t level) {
					u = std::clamp(u, 0.0f, 1.0f);
					v = std::clamp(v, 0.0f, 1.0f);

					uint32_t iu, iv, iw;

					// Quantize barycentric coordinates
					float fu = u * (1u << level);
					float fv = v * (1u << level);

					iu = (uint32_t)fu;
					iv = (uint32_t)fv;

					float uf = fu - float(iu);
					float vf = fv - float(iv);

					if (iu >= (1u << level)) iu = (1u << level) - 1u;
					if (iv >= (1u << level)) iv = (1u << level) - 1u;

					uint32_t iuv = iu + iv;

					if (iuv >= (1u << level))
						iu -= iuv - (1u << level) + 1u;

					iw = ~(iu + iv);

					if (uf + vf >= 1.0f && iuv < (1u << level) - 1u) --iw;

					uint32_t b0 = ~(iu ^ iw);
					b0 &= ((1u << level) - 1u);
					uint32_t t = (iu ^ iv) & b0;

					uint32_t f = t;
					f ^= f >> 1u;
					f ^= f >> 2u;
					f ^= f >> 4u;
					f ^= f >> 8u;

					uint32_t b1 = ((f ^ iu) & ~b0) | t;

					// Interleave bits
					b0 = (b0 | (b0 << 8u)) & 0x00ff00ffu;
					b0 = (b0 | (b0 << 4u)) & 0x0f0f0f0fu;
					b0 = (b0 | (b0 << 2u)) & 0x33333333u;
					b0 = (b0 | (b0 << 1u)) & 0x55555555u;
					b1 = (b1 | (b1 << 8u)) & 0x00ff00ffu;
					b1 = (b1 | (b1 << 4u)) & 0x0f0f0f0fu;
					b1 = (b1 | (b1 << 2u)) & 0x33333333u;
					b1 = (b1 | (b1 << 1u)) & 0x55555555u;

					return b0 | (b1 << 1u);
				}

				void OpacityMicromapBuilder::RegisterExtensionFunctions(VkDevice device) {
					vkCreateMicromapEXT = reinterpret_cast<PFN_vkCreateMicromapEXT>(vkGetDeviceProcAddr(device, "vkCreateMicromapEXT"));
					vkDestroyMicromapEXT = reinterpret_cast<PFN_vkDestroyMicromapEXT>(vkGetDeviceProcAddr(device, "vkDestroyMicromapEXT"));
					vkCmdBuildMicromapsEXT = reinterpret_cast<PFN_vkCmdBuildMicromapsEXT>(vkGetDeviceProcAddr(device, "vkCmdBuildMicromapsEXT"));
					vkGetMicromapBuildSizesEXT = reinterpret_cast<PFN_vkGetMicromapBuildSizesEXT>(vkGetDeviceProcAddr(device, "vkGetMicromapBuildSizesEXT"));
				}

				OpacityMicromapBuilder::~OpacityMicromapBuilder() {
					if (micromap != VK_NULL_HANDLE && vkDestroyMicromapEXT) {
						vkDestroyMicromapEXT(device, micromap, nullptr);
					}
				}

				int32_t OpacityMicromapBuilder::AddTriangle(const AlphaMask& mask, float alphaScale, float cutoff, glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2) {
					const std::array<int64_t, 8> key = {
						Quantize(uv0.x), Quantize(uv0.y), Quantize(uv1.x), Quantize(uv1.y), Quantize(uv2.x), Quantize(uv2.y),
						static_cast<int64_t>(reinterpret_cast<uintptr_t>(&mask)), Quantize(cutoff / std::max(alphaScale, 1e-6f))
					};
					if (auto it = cache.find(key); it != cache.end()) {
						return it->second;
					}

					auto known = classified.find(key);
					if (known == classified.end()) {
						known = classified.emplace(key, Classify(mask, alphaScale, cutoff, uv0, uv1, uv2)).first;
					}
					const Classified& result = known->second;

					int32_t index = result.special;
					if (result.special == 0) {
						VkMicromapTriangleEXT triangle{};
						triangle.dataOffset = static_cast<uint32_t>(data.size());
						triangle.subdivisionLevel = static_cast<uint16_t>(result.level);
						triangle.format = VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;
						data.insert(data.end(), result.data.begin(), result.data.end());
						index = static_cast<int32_t>(triangles.size());
						triangles.push_back(triangle);
					}

					cache.emplace(key, index);
					return index;
				}

				OpacityMicromapBuilder::Classified OpacityMicromapBuilder::Classify(const AlphaMask& mask, float alphaScale, float cutoff, glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2) {
					// Micro-triangles of about two texels
					const glm::vec2 texels(static_cast<float>(mask.width), static_cast<float>(mask.height));
					const float longestEdge = std::max({ glm::length((uv1 - uv0) * texels), glm::length((uv2 - uv1) * texels), glm::length((uv0 - uv2) * texels) });
					const uint32_t level = std::clamp(static_cast<uint32_t>(std::ceil(std::log2(std::max(longestEdge / MICRO_TRIANGLE_TEXELS, 1.0f)))), MIN_LEVEL, MAX_LEVEL);

					const uint32_t subdivisions = 1u << level;
					const uint32_t count = subdivisions * subdivisions;
					std::vector<uint8_t> states(count, 0xFF);

					auto uvAt = [&](float u, float v) { return uv0 * (1.0f - u - v) + uv1 * u + uv2 * v; };

					// A micro-triangle with corners a, b, c (barycentrics of vertices 1 and 2): sample it densely
					auto classify = [&](glm::vec2 a, glm::vec2 b, glm::vec2 c) {
						int opaque = 0, total = 0;
						for (int i = 0; i <= SAMPLES; i++) {
							for (int j = 0; j + i <= SAMPLES; j++) {
								const float s = static_cast<float>(i) / SAMPLES, t = static_cast<float>(j) / SAMPLES;
								const glm::vec2 bary = a * (1.0f - s - t) + b * s + c * t;
								const glm::vec2 uv = uvAt(bary.x, bary.y);
								opaque += alphaScale * mask.Sample(uv.x, uv.y) >= cutoff ? 1 : 0;
								total++;
							}
						}
						return opaque == total ? STATE_OPAQUE : opaque == 0 ? STATE_TRANSPARENT : STATE_UNKNOWN_OPAQUE;
					};

					auto store = [&](glm::vec2 a, glm::vec2 b, glm::vec2 c) {
						const glm::vec2 centroid = (a + b + c) / 3.0f;
						const uint32_t index = BarycentricsToSpaceFillingCurveIndex(centroid.x, centroid.y, level);
						if (index >= count || states[index] != 0xFF) {
							throw std::logic_error("Opacity micromap: micro-triangle index out of range or used twice");
						}
						states[index] = classify(a, b, c);
					};

					// The micro-triangle grid: "upright" triangles and the "inverted" ones between them
					const float step = 1.0f / static_cast<float>(subdivisions);
					for (uint32_t i = 0; i < subdivisions; i++) {
						for (uint32_t j = 0; i + j < subdivisions; j++) {
							const glm::vec2 corner(static_cast<float>(i) * step, static_cast<float>(j) * step);
							store(corner, corner + glm::vec2(step, 0.0f), corner + glm::vec2(0.0f, step));
							if (i + j + 1 < subdivisions) {
								store(corner + glm::vec2(step, 0.0f), corner + glm::vec2(0.0f, step), corner + glm::vec2(step, step));
							}
						}
					}

					// Uniform triangles need no entry
					bool allOpaque = true, allTransparent = true;
					for (uint8_t state : states) {
						allOpaque = allOpaque && state == STATE_OPAQUE;
						allTransparent = allTransparent && state == STATE_TRANSPARENT;
					}

					Classified result;
					result.level = level;
					if (allOpaque) {
						result.special = VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_OPAQUE_EXT;
					}
					else if (allTransparent) {
						result.special = VK_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_TRANSPARENT_EXT;
					}
					else {
						// 2 bits per micro-triangle, the first in the lowest bits of the first byte
						result.data.assign(count / 4, 0);
						for (uint32_t k = 0; k < count; k++) {
							result.data[k / 4] |= static_cast<uint8_t>(states[k] << ((k % 4) * 2));
						}
					}
					return result;
				}

				void OpacityMicromapBuilder::Build(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue) {
					this->device = device;
					if (triangles.empty()) {
						return;
					}

					const VkBufferUsageFlags inputUsage = VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
					const VkMemoryPropertyFlags hostMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

					Buffer dataBuffer(device, physicalDevice, data.size(), inputUsage, hostMemory);
					std::memcpy(dataBuffer.Map(), data.data(), data.size());
					dataBuffer.Unmap();

					Buffer triangleBuffer(device, physicalDevice, sizeof(VkMicromapTriangleEXT) * triangles.size(), inputUsage, hostMemory);
					std::memcpy(triangleBuffer.Map(), triangles.data(), sizeof(VkMicromapTriangleEXT) * triangles.size());
					triangleBuffer.Unmap();

					// How many entries of each subdivision level
					std::map<uint32_t, uint32_t> perLevel;
					for (const auto& triangle : triangles) {
						perLevel[triangle.subdivisionLevel]++;
					}
					std::vector<VkMicromapUsageEXT> usage;
					for (const auto& [level, count] : perLevel) {
						usage.push_back({ count, level, VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT });
					}

					VkMicromapBuildInfoEXT buildInfo{ VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT };
					buildInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
					buildInfo.flags = VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
					buildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
					buildInfo.usageCountsCount = static_cast<uint32_t>(usage.size());
					buildInfo.pUsageCounts = usage.data();
					buildInfo.data.deviceAddress = GetBufferDeviceAddress(device, dataBuffer.Get());
					buildInfo.triangleArray.deviceAddress = GetBufferDeviceAddress(device, triangleBuffer.Get());
					buildInfo.triangleArrayStride = sizeof(VkMicromapTriangleEXT);

					VkMicromapBuildSizesInfoEXT sizes{ VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT };
					vkGetMicromapBuildSizesEXT(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &sizes);

					storage = std::make_unique<Buffer>(device, physicalDevice, sizes.micromapSize,
						VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

					VkMicromapCreateInfoEXT createInfo{ VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT };
					createInfo.buffer = storage->Get();
					createInfo.size = sizes.micromapSize;
					createInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
					if (vkCreateMicromapEXT(device, &createInfo, nullptr, &micromap) != VK_SUCCESS) {
						throw std::runtime_error("Failed to create the opacity micromap");
					}

					Buffer scratch(device, physicalDevice, std::max<VkDeviceSize>(sizes.buildScratchSize, 4),
						VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

					buildInfo.dstMicromap = micromap;
					buildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(device, scratch.Get());

					CommandList commandList(device, commandPool);
					commandList.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
					vkCmdBuildMicromapsEXT(commandList.Get(), 1, &buildInfo);

					// Acceleration structure builds read the micromap
					VkMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
					barrier.srcStageMask = VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
					barrier.srcAccessMask = VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT;
					barrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
					barrier.dstAccessMask = VK_ACCESS_2_MICROMAP_READ_BIT_EXT;
					VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
					dependency.memoryBarrierCount = 1;
					dependency.pMemoryBarriers = &barrier;
					vkCmdPipelineBarrier2(commandList.Get(), &dependency);

					commandList.End().SubmitAndWait(queue);
				}
			}
		}
	}
}
