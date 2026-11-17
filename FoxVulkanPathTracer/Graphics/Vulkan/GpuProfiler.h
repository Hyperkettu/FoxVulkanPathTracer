#pragma once

#include <string>
#include <vector>

namespace Fox {

	namespace Graphics {

		namespace Vulkan {

			// Measures how long the GPU spends in each part of a frame, with timestamp queries.
			//
			// Each frame in flight has its own query pool. Results are read once that frame's fence has
			// signalled, so reading never stalls. Times are averaged over a short window for readability.
			//
			// Usage per frame:  BeginFrame -> Mark("pass A") -> Mark("pass B") ... (in submission order)
			//                   ReadResults after the frame's fence has signalled (i.e. before reusing the slot)
			class GpuProfiler {
			public:
				static constexpr uint32_t MAX_MARKS = 16;

				GpuProfiler(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t queueFamily, uint32_t framesInFlight);
				~GpuProfiler();

				GpuProfiler(const GpuProfiler&) = delete;
				GpuProfiler& operator=(const GpuProfiler&) = delete;

				bool IsSupported() const { return supported; }

				// Collects the timings of the frame that last used this slot. Call after its fence has signalled.
				void ReadResults(uint32_t frameSlot);

				// Starts recording the timings of a frame. Writes the reference timestamp.
				void BeginFrame(CommandList& commandList, uint32_t frameSlot);

				// Ends the section that started at the previous mark (or BeginFrame). Marks may be spread over
				// several command buffers of the frame, in submission order. idle: the GPU was waiting (e.g. for
				// a swapchain image), not working; such sections are left out of the frame time and the summary.
				void Mark(CommandList& commandList, const char* name, bool idle = false);

				// Average busy GPU time of the whole frame and of each section, in milliseconds.
				float GetFrameMilliseconds() const { return frameMilliseconds; }

				// e.g. "trace 8.1 | denoise 2.3 | tonemap 0.4"
				std::string GetSectionSummary() const;

			private:
				struct Slot {
					VkQueryPool pool = VK_NULL_HANDLE;
					std::vector<std::string> names; // Section names in the order they were marked
					std::vector<bool> idle;         // Per section, see Mark
					bool pending = false;           // Holds results not read yet
				};

				struct Section {
					std::string name;
					double sum = 0.0;  // Milliseconds summed over the current averaging window
					bool idle = false;
				};

				VkDevice device = VK_NULL_HANDLE;
				bool supported = false;
				double nanosecondsPerTick = 1.0;
				uint64_t timestampMask = ~0ull;

				std::vector<Slot> slots;
				Slot* recording = nullptr;

				// Averaging window
				std::vector<Section> sections;
				double frameSum = 0.0;
				uint32_t windowFrames = 0;

				// Published averages
				float frameMilliseconds = 0.0f;
				std::vector<std::pair<std::string, float>> averages;
			};

		}
	}
}
