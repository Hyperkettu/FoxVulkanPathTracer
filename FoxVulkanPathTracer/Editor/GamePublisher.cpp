#include "FoxRenderer.h"

#include "FoxVulkanPathTracer/Editor/GamePublisher.h"
#include "FoxVulkanPathTracer/Platform/GameConfig.h"
#include "FoxVulkanPathTracer/Core/TinyGLTF/json.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace Fox {

	namespace Editor {

		namespace fs = std::filesystem;

		namespace {
			fs::path ExecutablePath() {
#ifdef _WIN32
				wchar_t path[MAX_PATH];
				const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
				return fs::path(std::wstring(path, length));
#else
				return fs::current_path() / "FoxVulkanPathTracer";
#endif
			}

			// A name that works as a file name (letters, digits, spaces, - and _)
			std::string FileName(const std::string& name) {
				std::string out;
				for (char c : name) {
					const unsigned char u = static_cast<unsigned char>(c);
					if (std::isalnum(u) || c == ' ' || c == '-' || c == '_') out += c;
				}
				while (!out.empty() && out.back() == ' ') out.pop_back();
				while (!out.empty() && out.front() == ' ') out.erase(out.begin());
				return out.empty() ? std::string("Game") : out;
			}

			std::string Lower(std::string text) {
				std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return text;
			}

			// The folders a game reads, and which of their files it needs (extensions; empty: all but the excluded)
			struct AssetFolder {
				const char* name;
				std::vector<std::string> only;
				std::vector<std::string> except;
			};
			const std::vector<AssetFolder>& AssetFolders() {
				static const std::vector<AssetFolder> folders = {
					{ "Fonts", {}, {} },
					{ "Models", {}, { ".blend", ".blend1" } },   // Blender's own files are the sources, not the game's
					{ "Prefabs", {}, {} },
					{ "Scenes", {}, {} },
					{ "Scripts", { ".cs" }, {} },               // Compiled when the game starts
					{ "Settings", { ".json" }, {} },
					{ "Shaders", { ".spv" }, {} },              // Compiled ones only
					{ "Sounds", {}, {} },
					{ "Textures", {}, {} },
				};
				return folders;
			}

			// Under a build folder of a C# project (bin, obj): not a script of the game
			bool InBuildFolder(const fs::path& relative) {
				for (const auto& part : relative) {
					const std::string p = Lower(part.string());
					if (p == "bin" || p == "obj") return true;
				}
				return false;
			}

			std::string Megabytes(uint64_t bytes) {
				char text[32];
				std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
				return text;
			}
		}

		GamePublisher::GamePublisher() = default;

		GamePublisher::~GamePublisher() {
			if (worker.joinable()) {
				worker.join();
			}
		}

		void GamePublisher::Draw(bool* open, const Context& context, float uiScale) {
			if (!*open) {
				return;
			}
			// A name from the scene's, and a folder beside the project's
			if (!initialized) {
				initialized = true;
				std::string stem = fs::path(context.scenePath).stem().string();
				if (stem.empty() || stem == "default") stem = "My Game";
				std::replace(stem.begin(), stem.end(), '_', ' ');
				if (!stem.empty()) stem[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(stem[0])));
				std::snprintf(name, sizeof(name), "%s", stem.c_str());
				const std::string target = (fs::path("..") / "Published" / FileName(stem)).generic_string();
				std::snprintf(folder, sizeof(folder), "%s", target.c_str());
			}
			if (preset < 0) {
				preset = std::clamp(context.preset, 0, std::max(static_cast<int>(context.presets.size()) - 1, 0));
			}

			// Its width fixed, its height whatever it holds (the progress and the result below the button)
			const float width = 470.0f * uiScale;
			ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, FLT_MAX));
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (!ImGui::Begin("Publish Game", open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::End();
				return;
			}
			ImGui::PushTextWrapPos(width - ImGui::GetStyle().WindowPadding.x);
			const bool working = busy;
			ImGui::TextWrapped("The scene open in the editor, as it is now, made into a game that runs without the editor: "
				"it starts playing at once, through the scene's game camera.");
			ImGui::Spacing();

			ImGui::BeginDisabled(working);
			ImGui::SeparatorText("Game");
			ImGui::InputText("Name", name, sizeof(name));
			ImGui::InputText("Folder", folder, sizeof(folder));
			ImGui::SetItemTooltip("Where the game goes (from the project's folder). Published there before: replaced.");
			ImGui::TextDisabled("Start scene: %s (saved as it is now)", context.scenePath.empty() ? "the open scene" : context.scenePath.c_str());

			ImGui::SeparatorText("Start-up");
			ImGui::Checkbox("Fullscreen", &fullscreen);
			ImGui::SetItemTooltip("T switches between fullscreen and a window while the game runs");
			if (!context.presets.empty()) {
				const char* current = context.presets[std::clamp(preset, 0, static_cast<int>(context.presets.size()) - 1)].c_str();
				if (ImGui::BeginCombo("Render preset", current)) {
					for (int p = 0; p < static_cast<int>(context.presets.size()); p++) {
						if (ImGui::Selectable(context.presets[p].c_str(), p == preset)) preset = p;
					}
					ImGui::EndCombo();
				}
			}
			ImGui::TextDisabled("Render pipeline: %s", context.pipeline.c_str());
			ImGui::Spacing();
			if (ImGui::Button("Publish", ImVec2(120.0f * uiScale, 0.0f))) {
				Start(context);
			}
			ImGui::EndDisabled();

			// Progress, then the result
			if (busy) {
				const uint64_t total = std::max<uint64_t>(bytesTotal, 1);
				const float fraction = static_cast<float>(static_cast<double>(bytesDone) / static_cast<double>(total));
				const std::string label = std::to_string(filesDone.load()) + " / " + std::to_string(filesTotal.load()) + " files, "
					+ Megabytes(bytesDone) + " of " + Megabytes(bytesTotal);
				ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), label.c_str());
			}
			else {
				std::lock_guard<std::mutex> lock(mutex);
				if (!message.empty()) {
					ImGui::Spacing();
					if (succeeded) ImGui::TextWrapped("%s", message.c_str());
					else {
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.35f, 1.0f));
						ImGui::TextWrapped("%s", message.c_str());
						ImGui::PopStyleColor();
					}
#ifdef _WIN32
					if (succeeded && !published.empty()) {
						if (ImGui::Button("Open Folder")) {
							ShellExecuteW(nullptr, L"open", published.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
						}
						ImGui::SameLine();
						if (ImGui::Button("Run Game")) {
							const fs::path exe = published / executable;
							ShellExecuteW(nullptr, L"open", exe.wstring().c_str(), nullptr, published.wstring().c_str(), SW_SHOWNORMAL);
						}
					}
#endif
				}
			}
			ImGui::PopTextWrapPos();
			ImGui::End();
		}

		void GamePublisher::Start(const Context& context) {
			if (busy || !context.scene) {
				return;
			}
			if (worker.joinable()) {
				worker.join();
			}
			auto fail = [&](const std::string& why) {
				std::lock_guard<std::mutex> lock(mutex);
				message = why;
				succeeded = false;
			};

			const std::string gameName = std::string(name).empty() ? std::string("Game") : std::string(name);
			const std::string fileName = FileName(gameName);
			std::error_code error;
			const fs::path project = fs::current_path();
			const fs::path output = fs::weakly_canonical(fs::absolute(fs::path(folder)), error);
			if (std::string(folder).empty() || output.empty()) {
				fail("Choose a folder for the game.");
				return;
			}
			// Not into the project (it would copy itself) nor over something that is not a game published before
			const fs::path relative = fs::relative(output, project, error);
			if (output == project || (!relative.empty() && relative.native()[0] != L'.')) {
				fail("The game's folder must be outside the project's folder.");
				return;
			}
			if (fs::exists(output, error) && !fs::is_empty(output, error) && !fs::exists(output / Fox::Platform::GAME_CONFIG_FILE, error)) {
				fail(output.string() + " has other files in it: choose an empty folder or a new one.");
				return;
			}

			// The scene as it is now, saved where the game finds it (paths in scene files are from the game's folder:
			// saved into the project, copied with its other scenes, then taken out of the project again)
			const std::string startScene = "Scenes/Start/" + fileName + ".json";
			Fox::Scene::RayTracing::MainRayTracingScene::CameraPose pose;
			pose.position = context.cameraPosition;
			pose.target = context.cameraTarget;
			std::string saveError;
			if (!context.scene->SaveToFile(startScene, pose, saveError)) {
				fail("Cannot save the scene: " + saveError);
				return;
			}
			std::vector<fs::path> temporary;
			for (const auto& entry : fs::directory_iterator("Scenes/Start", error)) {
				if (entry.path().stem().string().rfind(fileName, 0) == 0) temporary.push_back(entry.path());
			}

			// What goes into the game
			std::vector<Job> jobs;
			auto add = [&](const fs::path& source, const fs::path& target) {
				std::error_code sizeError;
				const uintmax_t size = fs::file_size(source, sizeError);
				jobs.push_back({ source, target, sizeError ? 0 : size });
			};
			const fs::path exe = ExecutablePath();
			const fs::path exeDirectory = exe.parent_path();
			const std::string exeName = fileName + exe.extension().string();
			add(exe, exeName);
			for (const auto& entry : fs::directory_iterator(exeDirectory, error)) {
				if (entry.is_regular_file() && Lower(entry.path().extension().string()) == ".dll") add(entry.path(), entry.path().filename());
			}
			// The C# host (beside the executable)
			for (auto it = fs::recursive_directory_iterator(exeDirectory / "Scripting", error); !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
				const std::string extension = Lower(it->path().extension().string());
				if (it->is_regular_file() && extension != ".pdb" && extension != ".xml") {
					add(it->path(), fs::path("Scripting") / fs::relative(it->path(), exeDirectory / "Scripting"));
				}
			}
			error.clear();
			// The assets (from the project's folder, the working directory)
			for (const AssetFolder& asset : AssetFolders()) {
				if (!fs::exists(asset.name, error)) continue;
				for (auto it = fs::recursive_directory_iterator(asset.name, error); !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
					if (!it->is_regular_file()) continue;
					const fs::path relativePath = it->path().lexically_normal();
					const std::string extension = Lower(relativePath.extension().string());
					if (!asset.only.empty() && std::find(asset.only.begin(), asset.only.end(), extension) == asset.only.end()) continue;
					if (std::find(asset.except.begin(), asset.except.end(), extension) != asset.except.end()) continue;
					if (std::string(asset.name) == "Scripts" && InBuildFolder(relativePath)) continue;
					if (std::string(asset.name) == "Settings" && relativePath.filename() == "Render.json") continue; // The game's own
					add(it->path(), relativePath);
				}
				error.clear();
			}

			// The game's start-up, and its render settings
			nlohmann::json game;
			game["name"] = gameName;
			game["scene"] = startScene;
			game["fullscreen"] = fullscreen;
			nlohmann::json render;
			render["preset"] = context.presets.empty() ? std::string("Auto") : context.presets[std::clamp(preset, 0, static_cast<int>(context.presets.size()) - 1)];
			render["pipeline"] = context.pipeline;

			{
				std::lock_guard<std::mutex> lock(mutex);
				message.clear();
				executable = exeName;
			}
			uint64_t total = 0;
			for (const Job& job : jobs) total += job.bytes;
			bytesTotal = total;
			bytesDone = 0;
			filesTotal = static_cast<uint32_t>(jobs.size());
			filesDone = 0;
			busy = true;
			worker = std::thread(&GamePublisher::Run, this, std::move(jobs), output, game.dump(2), render.dump(2), std::move(temporary));
		}

		void GamePublisher::Run(std::vector<Job> jobs, fs::path output, std::string gameConfig, std::string renderSettings,
			std::vector<fs::path> temporary) {
			std::string failure;
			std::error_code error;
			// A game published there before: replaced as a whole (no files left from it)
			if (fs::exists(output / Fox::Platform::GAME_CONFIG_FILE, error)) {
				fs::remove_all(output, error);
				if (error) failure = "Cannot replace " + output.string() + ": " + error.message() + " (is the game running?)";
			}
			for (const Job& job : jobs) {
				if (!failure.empty()) break;
				const fs::path target = output / job.target;
				fs::create_directories(target.parent_path(), error);
				if (!fs::copy_file(job.source, target, fs::copy_options::overwrite_existing, error) || error) {
					failure = "Cannot copy " + job.source.string() + ": " + error.message();
					break;
				}
				bytesDone += job.bytes;
				filesDone++;
			}
			if (failure.empty()) {
				std::ofstream(output / Fox::Platform::GAME_CONFIG_FILE) << gameConfig << "\n";
				fs::create_directories(output / "Settings", error);
				std::ofstream(output / "Settings" / "Render.json") << renderSettings << "\n";
			}
			// The start scene saved for it is the game's alone
			for (const fs::path& file : temporary) {
				fs::remove(file, error);
			}
			if (fs::is_empty("Scenes/Start", error)) {
				fs::remove("Scenes/Start", error);
			}

			{
				std::lock_guard<std::mutex> lock(mutex);
				succeeded = failure.empty();
				if (succeeded) {
					published = output;
					message = "Published to " + output.string() + ": " + std::to_string(jobs.size() + 2) + " files, " + Megabytes(bytesTotal)
						+ ". Run " + executable + " there (it needs the .NET runtime for its scripts, as the editor does).";
				}
				else {
					message = failure;
				}
			}
			std::cout << "Publish: " << (succeeded ? "published to " + output.string() : failure) << "\n";
			busy = false;
		}
	}
}
