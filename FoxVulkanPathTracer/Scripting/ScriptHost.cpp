#include "FoxRenderer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// The .NET hosting API's headers: copied from the SDK by the scripting host's build (x64/nethost, see
// FoxScripting.csproj)
#include <hostfxr.h>
#include <coreclr_delegates.h>

#include <vector>

namespace Fox {

	namespace Scripting {

		namespace {
			std::filesystem::path ExecutableDirectory() {
				wchar_t path[MAX_PATH];
				const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
				return std::filesystem::path(std::wstring(path, length)).parent_path();
			}

			// hostfxr.dll of the newest .NET installed: in DOTNET_ROOT, or where the installer puts it (what nethost does,
			// without linking its static library, which is built for another C++ runtime)
			std::filesystem::path FindHostfxr() {
				auto variable = [](const wchar_t* name) {
					wchar_t value[MAX_PATH];
					const DWORD length = GetEnvironmentVariableW(name, value, MAX_PATH);
					return length > 0 && length < MAX_PATH ? std::wstring(value, length) : std::wstring();
				};
				std::vector<std::filesystem::path> roots;
				if (const std::wstring root = variable(L"DOTNET_ROOT"); !root.empty()) {
					roots.emplace_back(root);
				}
				if (const std::wstring programFiles = variable(L"ProgramFiles"); !programFiles.empty()) {
					roots.emplace_back(std::filesystem::path(programFiles) / "dotnet");
				}
				for (const auto& root : roots) {
					std::error_code failed;
					std::filesystem::path best;
					std::vector<int> bestVersion;
					for (const auto& entry : std::filesystem::directory_iterator(root / "host" / "fxr", failed)) {
						if (!std::filesystem::exists(entry.path() / "hostfxr.dll")) {
							continue;
						}
						// Versions compare part by part (10.0.11 is newer than 9.0.5)
						std::vector<int> version;
						std::string name = entry.path().filename().string();
						for (size_t start = 0; start <= name.size();) {
							const size_t dot = name.find_first_of(".-", start);
							version.push_back(std::atoi(name.substr(start, dot - start).c_str()));
							if (dot == std::string::npos || name[dot] == '-') break;
							start = dot + 1;
						}
						if (best.empty() || version > bestVersion) {
							best = entry.path() / "hostfxr.dll";
							bestVersion = version;
						}
					}
					if (!best.empty()) {
						return best;
					}
				}
				return {};
			}

			bool IsScriptFile(const std::filesystem::path& path) {
				if (path.extension() != ".cs") {
					return false;
				}
				for (const auto& part : path) {
					if (part == "obj" || part == "bin") {
						return false; // An IDE project's build output
					}
				}
				return true;
			}
		}

		bool ScriptHost::Initialize(const std::filesystem::path& scriptsDirectory, const NativeApi& api) {
			directory = std::filesystem::absolute(scriptsDirectory);
			native = api;

			const std::filesystem::path hostDirectory = ExecutableDirectory() / "Scripting";
			const std::filesystem::path assembly = hostDirectory / "FoxScripting.dll";
			const std::filesystem::path config = hostDirectory / "FoxScripting.runtimeconfig.json";
			if (!std::filesystem::exists(assembly) || !std::filesystem::exists(config)) {
				error = "the scripting host is not built (" + assembly.string() + ")";
				return false;
			}

			// hostfxr of the installed runtime
			const std::filesystem::path hostfxrPath = FindHostfxr();
			if (hostfxrPath.empty()) {
				error = "no .NET runtime found (install .NET 10)";
				return false;
			}
			HMODULE hostfxr = LoadLibraryW(hostfxrPath.c_str());
			if (!hostfxr) {
				error = "could not load hostfxr";
				return false;
			}
			auto initialize = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(GetProcAddress(hostfxr, "hostfxr_initialize_for_runtime_config"));
			auto getDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(GetProcAddress(hostfxr, "hostfxr_get_runtime_delegate"));
			auto close = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(hostfxr, "hostfxr_close"));
			if (!initialize || !getDelegate || !close) {
				error = "hostfxr is missing functions";
				return false;
			}

			hostfxr_handle context = nullptr;
			const int started = initialize(config.c_str(), nullptr, &context);
			if (started < 0 || !context) {
				char text[64];
				std::snprintf(text, sizeof(text), "the .NET runtime did not start (0x%08x)", static_cast<unsigned>(started));
				error = text;
				if (context) {
					close(context);
				}
				return false;
			}
			load_assembly_and_get_function_pointer_fn load = nullptr;
			getDelegate(context, hdt_load_assembly_and_get_function_pointer, reinterpret_cast<void**>(&load));
			close(context);
			if (!load) {
				error = "the .NET runtime gave no loader";
				return false;
			}

			using Bootstrap = int32_t(CORECLR_DELEGATE_CALLTYPE*)(ManagedApi*);
			Bootstrap bootstrap = nullptr;
			const int loaded = load(assembly.c_str(), L"Fox.Host, FoxScripting", L"Bootstrap", UNMANAGEDCALLERSONLY_METHOD, nullptr,
				reinterpret_cast<void**>(&bootstrap));
			if (loaded != 0 || !bootstrap || !bootstrap(&managed)) {
				char text[96];
				std::snprintf(text, sizeof(text), "the scripting host did not load (0x%08x)", static_cast<unsigned>(loaded));
				error = text;
				return false;
			}

			ready = true;
			std::filesystem::create_directories(directory);
			stamps = Scan();
			const std::u8string text = directory.u8string();
			const std::string folder(reinterpret_cast<const char*>(text.c_str()), text.size());
			managed.Initialize(&native, folder.c_str()); // Compile errors are reported, and in GetInfo
			reloads++;
			infoValid = false;
			return true;
		}

		std::map<std::string, std::filesystem::file_time_type> ScriptHost::Scan() const {
			std::map<std::string, std::filesystem::file_time_type> files;
			std::error_code failed;
			for (auto it = std::filesystem::recursive_directory_iterator(directory, failed); !failed && it != std::filesystem::recursive_directory_iterator(); it.increment(failed)) {
				if (it->is_regular_file(failed) && IsScriptFile(it->path())) {
					files[it->path().string()] = it->last_write_time(failed);
				}
			}
			return files;
		}

		bool ScriptHost::Poll(float deltaTime) {
			if (!ready) {
				return false;
			}
			pollTimer += deltaTime;
			if (pollTimer < 0.25f) {
				return false;
			}
			pollTimer = 0.0f;
			auto now = Scan();
			if (now == stamps) {
				return false;
			}
			stamps = std::move(now);
			return Reload();
		}

		bool ScriptHost::Reload() {
			if (!ready) {
				return false;
			}
			const bool compiled = managed.Reload() != 0;
			reloads++;
			infoValid = false;
			return compiled;
		}

		int64_t ScriptHost::CreateInstance(uint64_t entity, const std::string& className, const std::string& fieldsJson) {
			return ready ? managed.CreateInstance(entity, className.c_str(), fieldsJson.c_str()) : 0;
		}

		void ScriptHost::DestroyInstance(int64_t instance) {
			if (ready && instance != 0) {
				managed.DestroyInstance(instance);
			}
		}

		void ScriptHost::Update(float deltaTime) {
			if (ready) {
				managed.Update(deltaTime);
			}
		}

		void ScriptHost::StopAll() {
			if (ready) {
				managed.StopAll();
			}
		}

		const std::string& ScriptHost::GetInfo() {
			if (!ready) {
				static const std::string none = "{}";
				return none;
			}
			if (!infoValid) {
				std::vector<char> buffer(16384);
				int32_t length = managed.GetInfo(buffer.data(), static_cast<int32_t>(buffer.size()));
				if (length > static_cast<int32_t>(buffer.size())) {
					buffer.resize(length);
					length = managed.GetInfo(buffer.data(), length);
				}
				info.assign(buffer.data(), std::max(length, 0));
				infoValid = true;
			}
			return info;
		}
	}
}
