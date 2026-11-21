#pragma once

#include <string>

namespace Fox {

	namespace Platform {

		// A published game (File > Publish Game, Editor/GamePublisher): Game.json beside the executable says the engine
		// runs as that game. No editor then: it starts playing the start scene at once, the keys are the game's (Escape
		// quits, T switches fullscreen), the window has the game's name, and the console's output goes to Game.log.
		struct GameConfig {
			bool enabled = false;          // Game.json was found
			std::string name = "Fox Engine";
			std::string scene;             // The start scene (relative to the game's folder)
			bool fullscreen = true;
		};

		// Read once, on first use (first thing in main): Game.json beside the executable (the game's folder becomes the
		// working directory, wherever it was started from), else in the working directory
		const GameConfig& GetGameConfig();

		// The file name a published game reads
		constexpr const char* GAME_CONFIG_FILE = "Game.json";
	}
}
