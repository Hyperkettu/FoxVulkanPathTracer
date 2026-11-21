#include "FoxRenderer.h"

#include "FoxVulkanPathTracer/Editor/LogWindow.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace Fox {

	namespace Editor {

		namespace Log {

			namespace {
				constexpr size_t MAX_LINES = 5000; // The oldest go

				enum class Kind { Normal, Warning, Error, Timing };

				struct Line {
					std::string text;
					char time[9] = {}; // hh:mm:ss
					Kind kind = Kind::Normal;
				};

				Kind KindOf(const std::string& text) {
					std::string lower(text);
					std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (lower.rfind("[gpu]", 0) == 0 || lower.rfind("[cpu]", 0) == 0 || lower.rfind("[hybrid]", 0) == 0) return Kind::Timing;
					for (const char* word : { "error", "failed", "fatal", "cannot", "exception", "not found" }) {
						if (lower.find(word) != std::string::npos) return Kind::Error;
					}
					if (lower.find("warning") != std::string::npos) return Kind::Warning;
					return Kind::Normal;
				}

				// The lines, filled from any thread
				class CaptureBuffer : public std::streambuf {
				public:
					std::mutex mutex;
					std::deque<Line> lines;
					uint64_t added = 0; // Lines ever added (the window scrolls when it grows)

				protected:
					int overflow(int c) override {
						if (c != traits_type::eof()) {
							const char ch = traits_type::to_char_type(c);
							std::lock_guard<std::mutex> lock(mutex);
							Put(&ch, 1);
						}
						return traits_type::not_eof(c);
					}
					std::streamsize xsputn(const char* text, std::streamsize count) override {
						std::lock_guard<std::mutex> lock(mutex);
						Put(text, static_cast<size_t>(count));
						return count;
					}

				private:
					std::string pending; // The line being written

					void Put(const char* text, size_t count) {
						for (size_t i = 0; i < count; i++) {
							if (text[i] == '\n') {
								Finish();
							}
							else if (text[i] != '\r') {
								pending += text[i];
							}
						}
					}
					void Finish() {
						Line line;
						line.text = std::move(pending);
						pending.clear();
						line.kind = KindOf(line.text);
						const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
						std::tm local{};
						localtime_s(&local, &now);
						std::strftime(line.time, sizeof(line.time), "%H:%M:%S", &local);
						lines.push_back(std::move(line));
						if (lines.size() > MAX_LINES) lines.pop_front();
						added++;
					}
				};

				CaptureBuffer& Buffer() {
					static CaptureBuffer* buffer = new CaptureBuffer(); // Never destroyed: printing may go on while the program exits
					return *buffer;
				}

				// The window's settings
				ImGuiTextFilter filter;
				bool showTimings = false; // The renderer's [GPU] lines every 2 seconds
				bool autoScroll = true;
				uint64_t seen = 0;
			}

			std::streambuf* Capture() {
				return &Buffer();
			}

			void Draw(bool* open) {
				if (!ImGui::Begin("Log", open)) {
					ImGui::End();
					return;
				}
				CaptureBuffer& buffer = Buffer();

				// --- Tools ---
				if (ImGui::Button("Clear")) {
					std::lock_guard<std::mutex> lock(buffer.mutex);
					buffer.lines.clear();
				}
				ImGui::SameLine();
				const bool copy = ImGui::Button("Copy");
				ImGui::SetItemTooltip("The lines shown, to the clipboard");
				ImGui::SameLine();
				ImGui::Checkbox("Timings", &showTimings);
				ImGui::SetItemTooltip("The renderer's [GPU] lines (every 2 seconds)");
				ImGui::SameLine();
				ImGui::Checkbox("Auto-scroll", &autoScroll);
				ImGui::SameLine();
				filter.Draw("Filter", std::max(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Filter").x - ImGui::GetStyle().ItemInnerSpacing.x, 60.0f));
				ImGui::Separator();

				// --- The lines ---
				const ImVec4 colors[4] = {
					ImGui::GetStyleColorVec4(ImGuiCol_Text),
					ImVec4(1.0f, 0.72f, 0.3f, 1.0f),  // Warnings
					ImVec4(1.0f, 0.45f, 0.38f, 1.0f), // Errors
					ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
				};
				ImGui::BeginChild("Lines", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
				{
					std::lock_guard<std::mutex> lock(buffer.mutex);
					std::vector<int> shown;
					shown.reserve(buffer.lines.size());
					for (int i = 0; i < static_cast<int>(buffer.lines.size()); i++) {
						const Line& line = buffer.lines[i];
						if (!showTimings && line.kind == Kind::Timing) continue;
						if (!filter.PassFilter(line.text.c_str())) continue;
						shown.push_back(i);
					}
					if (copy) {
						std::string all;
						for (int i : shown) all += std::string(buffer.lines[i].time) + "  " + buffer.lines[i].text + "\n";
						ImGui::SetClipboardText(all.c_str());
					}
					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f));
					ImGuiListClipper clipper;
					clipper.Begin(static_cast<int>(shown.size()));
					while (clipper.Step()) {
						for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
							const Line& line = buffer.lines[shown[row]];
							ImGui::TextDisabled("%s", line.time);
							ImGui::SameLine();
							ImGui::PushStyleColor(ImGuiCol_Text, colors[static_cast<int>(line.kind)]);
							ImGui::TextUnformatted(line.text.c_str());
							ImGui::PopStyleColor();
						}
					}
					ImGui::PopStyleVar();
					// New lines: to the bottom, unless scrolled up to read
					if (buffer.added != seen) {
						const bool first = seen == 0; // Opened: at the newest
						seen = buffer.added;
						if (autoScroll && (first || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetTextLineHeightWithSpacing() * 3.0f)) {
							ImGui::SetScrollHereY(1.0f);
						}
					}
				}
				ImGui::EndChild();
				ImGui::End();
			}
		}
	}
}
