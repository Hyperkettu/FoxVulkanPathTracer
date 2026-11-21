#include "FoxRenderer.h"

#include "FoxVulkanPathTracer/Platform/GameConfig.h"
#include "FoxVulkanPathTracer/Core/TinyGLTF/json.hpp"

#include <filesystem>
#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Fox {

	namespace Platform {

		namespace {
			std::filesystem::path ExecutableDirectory() {
#ifdef _WIN32
				wchar_t path[MAX_PATH];
				const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
				return std::filesystem::path(std::wstring(path, length)).parent_path();
#else
				return std::filesystem::current_path();
#endif
			}

			GameConfig Load() {
				GameConfig config;
				std::error_code error;
				std::filesystem::path file = ExecutableDirectory() / GAME_CONFIG_FILE;
				if (std::filesystem::exists(file, error)) {
					// Its assets are beside it, whatever folder it was started from (a shortcut, another program)
					std::filesystem::current_path(file.parent_path(), error);
				}
				else {
					file = GAME_CONFIG_FILE;
					if (!std::filesystem::exists(file, error)) {
						return config;
					}
				}
				try {
					std::ifstream in(file);
					const nlohmann::json j = nlohmann::json::parse(in, nullptr, true, true);
					config.enabled = true;
					config.name = j.value("name", config.name);
					config.scene = j.value("scene", config.scene);
					config.fullscreen = j.value("fullscreen", config.fullscreen);
				}
				catch (const std::exception& e) {
					std::cout << GAME_CONFIG_FILE << ": " << e.what() << "\n";
				}
				return config;
			}
		}

		const GameConfig& GetGameConfig() {
			static const GameConfig config = Load();
			return config;
		}
	}
}
