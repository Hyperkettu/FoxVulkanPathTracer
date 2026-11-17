#include "FoxRenderer.h"

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			namespace {
				// Frames averaged before the published numbers are updated
				const uint32_t AVERAGING_FRAMES = 30;
			}

			GpuProfiler::GpuProfiler(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t queueFamily, uint32_t framesInFlight)
				: device(device)
			{
				VkPhysicalDeviceProperties properties{};
				vkGetPhysicalDeviceProperties(physicalDevice, &properties);

				uint32_t familyCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
				std::vector<VkQueueFamilyProperties> families(familyCount);
				vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

				const uint32_t validBits = queueFamily < familyCount ? families[queueFamily].timestampValidBits : 0;
				supported = validBits > 0 && properties.limits.timestampPeriod > 0.0f;
				if (!supported) {
					return;
				}

				nanosecondsPerTick = properties.limits.timestampPeriod;
				timestampMask = validBits >= 64 ? ~0ull : ((1ull << validBits) - 1);

				slots.resize(framesInFlight);
				for (Slot& slot : slots) {
					VkQueryPoolCreateInfo info{};
					info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
					info.queryType = VK_QUERY_TYPE_TIMESTAMP;
					info.queryCount = MAX_MARKS + 1;

					if (vkCreateQueryPool(device, &info, nullptr, &slot.pool) != VK_SUCCESS) {
						supported = false;
						return;
					}
				}
			}

			GpuProfiler::~GpuProfiler() {
				for (Slot& slot : slots) {
					if (slot.pool) {
						vkDestroyQueryPool(device, slot.pool, nullptr);
					}
				}
			}

			void GpuProfiler::BeginFrame(CommandList& commandList, uint32_t frameSlot) {
				if (!supported) {
					return;
				}

				recording = &slots[frameSlot];
				recording->names.clear();
				recording->idle.clear();
				recording->pending = true;

				vkCmdResetQueryPool(commandList.Get(), recording->pool, 0, MAX_MARKS + 1);
				vkCmdWriteTimestamp(commandList.Get(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, recording->pool, 0);
			}

			void GpuProfiler::Mark(CommandList& commandList, const char* name, bool idle) {
				if (!supported || !recording || recording->names.size() >= MAX_MARKS) {
					return;
				}

				recording->names.emplace_back(name);
				recording->idle.push_back(idle);
				vkCmdWriteTimestamp(commandList.Get(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, recording->pool, static_cast<uint32_t>(recording->names.size()));
			}

			void GpuProfiler::ReadResults(uint32_t frameSlot) {
				if (!supported || frameSlot >= slots.size()) {
					return;
				}

				Slot& slot = slots[frameSlot];
				if (!slot.pending || slot.names.empty()) {
					return;
				}
				slot.pending = false;

				const uint32_t count = static_cast<uint32_t>(slot.names.size()) + 1;
				uint64_t ticks[MAX_MARKS + 1] = {};

				// The fence of this frame has signalled, so the results are available: no wait flag needed
				if (vkGetQueryPoolResults(device, slot.pool, 0, count, sizeof(ticks), ticks, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
					return;
				}

				auto milliseconds = [this](uint64_t from, uint64_t to) {
					return static_cast<double>((to - from) & timestampMask) * nanosecondsPerTick * 1e-6;
				};

				// The set of sections can change between frames (other denoiser): start a new window then
				bool sameSections = sections.size() == slot.names.size();
				for (size_t i = 0; sameSections && i < sections.size(); i++) {
					sameSections = sections[i].name == slot.names[i];
				}
				if (!sameSections) {
					sections.clear();
					for (size_t i = 0; i < slot.names.size(); i++) {
						sections.push_back({ slot.names[i], 0.0, slot.idle[i] });
					}
					frameSum = 0.0;
					windowFrames = 0;
				}

				// Busy time only: idle sections (waiting for the display) are left out
				for (size_t i = 0; i < sections.size(); i++) {
					const double section = milliseconds(ticks[i], ticks[i + 1]);
					sections[i].sum += section;
					if (!sections[i].idle) {
						frameSum += section;
					}
				}
				windowFrames++;

				if (windowFrames >= AVERAGING_FRAMES) {
					frameMilliseconds = static_cast<float>(frameSum / windowFrames);
					averages.clear();
					for (Section& section : sections) {
						if (section.idle) {
							section.sum = 0.0;
							continue;
						}
						averages.emplace_back(section.name, static_cast<float>(section.sum / windowFrames));
						section.sum = 0.0;
					}
					frameSum = 0.0;
					windowFrames = 0;
				}
			}

			std::string GpuProfiler::GetSectionSummary() const {
				std::ostringstream text;
				text << std::fixed << std::setprecision(1);
				for (size_t i = 0; i < averages.size(); i++) {
					text << (i > 0 ? " | " : "") << averages[i].first << " " << averages[i].second;
				}
				return text.str();
			}

		}
	}
}
