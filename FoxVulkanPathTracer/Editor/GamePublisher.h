#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Fox {

	namespace Scene::RayTracing { class MainRayTracingScene; }

	namespace Editor {

		// File > Publish Game: the scene open in the editor made into a game that runs without the editor. A folder
		// with the engine's executable (named after the game), its libraries and C# host, the assets a game reads
		// (models, textures, scenes, scripts, sounds, fonts, prefabs, compiled shaders, settings) and Game.json, which
		// makes the engine start as that game (Platform/GameConfig). The copying runs on a thread of its own.
		class GamePublisher {
		public:
			struct Context {
				Fox::Scene::RayTracing::MainRayTracingScene* scene = nullptr;
				glm::vec3 cameraPosition{ 0.0f };   // The editor's view (saved with the scene)
				glm::vec3 cameraTarget{ 0.0f, 0.0f, -1.0f };
				std::string scenePath;              // The scene file open in the editor (its name is the default game name)
				std::vector<std::string> presets;   // Render presets the game can start with
				int preset = 0;                     // The editor's
				std::string pipeline;               // The editor's render pipeline (the game keeps it)
			};

			GamePublisher();
			~GamePublisher();

			void Draw(bool* open, const Context& context, float uiScale);

		private:
			struct Job {
				std::filesystem::path source, target;
				uintmax_t bytes = 0;
			};

			void Start(const Context& context);
			void Run(std::vector<Job> jobs, std::filesystem::path output, std::string gameConfig, std::string renderSettings,
				std::vector<std::filesystem::path> temporary);

			char name[64] = {};
			char folder[512] = {};
			bool fullscreen = true;
			int preset = -1;
			bool initialized = false;

			std::thread worker;
			std::atomic<bool> busy{ false };
			std::atomic<uint64_t> bytesDone{ 0 }, bytesTotal{ 0 };
			std::atomic<uint32_t> filesDone{ 0 }, filesTotal{ 0 };
			std::mutex mutex;
			std::string message;       // The last result
			bool succeeded = false;
			std::filesystem::path published; // The folder of the last game published
			std::string executable;          // Its executable's file name
		};
	}
}
