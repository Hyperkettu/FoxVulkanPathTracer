#pragma once

namespace Fox {

	namespace Graphics {

		struct RendererConfig {
			uint32_t MAX_FRAMES_IN_FLIGHT = 2;

			std::vector<const char*> instanceExtensions;
			std::vector<const char*> layers;

			void* windowHandle = nullptr; // SDL_Window*
		};

	}
}
