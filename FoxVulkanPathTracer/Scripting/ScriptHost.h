#pragma once

#include <filesystem>
#include <map>
#include <string>

#include "FoxVulkanPathTracer/Scripting/ScriptApi.h"

namespace Fox {

	namespace Scripting {

		// Runs C# scripts: starts the .NET runtime in the process (the hosting API, hostfxr) and loads the scripting
		// host (Scripting/FoxScripting, built beside the executable in Scripting/), which compiles the game's
		// scripts (Scripts/*.cs) with Roslyn. Watches the scripts' files: when one is saved, Poll reloads them.
		//
		// Without a .NET runtime (or the host assembly) it stays off: IsReady is false and the reason is in GetError.
		class ScriptHost {
		public:
			ScriptHost() = default;
			ScriptHost(const ScriptHost&) = delete;
			ScriptHost& operator=(const ScriptHost&) = delete;

			// Starts .NET and compiles the scripts in scriptsDirectory. api: the engine's functions (kept)
			bool Initialize(const std::filesystem::path& scriptsDirectory, const NativeApi& api);
			bool IsReady() const { return ready; }
			const std::string& GetError() const { return error; }

			// Every frame: reloads the scripts when a file in the folder changed (checked a few times a second).
			// Returns true when it reloaded them.
			bool Poll(float deltaTime);
			bool Reload();

			int64_t CreateInstance(uint64_t entity, const std::string& className, const std::string& fieldsJson);
			void DestroyInstance(int64_t instance);
			void Update(float deltaTime);
			void StopAll();
			void Contact(uint64_t entity, int32_t kind, uint64_t other, const float* data7);

			// Classes, their fields and compile errors, as JSON (see Host.GetInfo); kept until the next reload
			const std::string& GetInfo();
			uint64_t GetInfoGeneration() const { return reloads; }

			const std::filesystem::path& GetScriptsDirectory() const { return directory; }

		private:
			bool ready = false;
			std::string error;
			std::filesystem::path directory;
			NativeApi native{};
			ManagedApi managed{};
			std::string info;
			bool infoValid = false;
			uint64_t reloads = 0;

			// What the folder held when last compiled: file -> last write time
			std::map<std::string, std::filesystem::file_time_type> stamps;
			float pollTimer = 0.0f;
			std::map<std::string, std::filesystem::file_time_type> Scan() const;
		};
	}
}
