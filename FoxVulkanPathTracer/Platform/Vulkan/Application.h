#pragma once 

#include "FoxVulkanPathTracer/Platform/IApplication.h"
#include "FoxVulkanPathTracer/Graphics/RHI.h"

namespace Fox {

	namespace Platform {

		namespace Vulkan {
	
			class Application : public Fox::Platform::IApplication {
			public: 
				Application();
				Application(const std::string& title, uint32_t width, uint32_t height) : title(title), width(width), height(height) {}


				~Application();

				int32_t Initialize() override;
				void Run() override;

				void ToggleFullscreen(SDL_Window* window, bool& isFullscreen) {
					isFullscreen = !isFullscreen;

					// Choose your flavor: SDL_WINDOW_FULLSCREEN or SDL_WINDOW_FULLSCREEN_DESKTOP
					uint32_t flags = isFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;

					SDL_SetWindowFullscreen(window, flags);
				}

				void ToggleScreen(SDL_Scancode key);
					

			private: 
				SDL_Window* window;

				std::string title;
				uint32_t width;
				uint32_t height;

				std::unique_ptr<Fox::Graphics::RHI> currentRHI;
				std::unique_ptr<Fox::Input::InputManager> inputManager;

				Fox::Core::Connection fullscreenToggleBind;

				bool isFullscreen = false;

			};
		
		}
	
	}

}