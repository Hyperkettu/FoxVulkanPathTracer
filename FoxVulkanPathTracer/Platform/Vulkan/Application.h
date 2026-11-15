#pragma once

#include "FoxVulkanPathTracer/Platform/IApplication.h"
#include "FoxVulkanPathTracer/Graphics/RHI.h"

namespace Fox {

	namespace Platform {

		namespace Vulkan {

			// SDL window + main loop driving the Vulkan renderer.
			class Application : public Fox::Platform::IApplication {
			public:
				Application(const std::string& title, uint32_t width, uint32_t height) : title(title), width(width), height(height) {}

				~Application();

				bool Initialize() override;
				void Run() override;

			private:
				void OnKeyReleased(SDL_Scancode key);
				void ToggleFullscreen();

				SDL_Window* window = nullptr;

				std::string title;
				uint32_t width = 0;
				uint32_t height = 0;

				// Declared before the renderer so it outlives it: the renderer holds input connections
				std::unique_ptr<Fox::Input::InputManager> inputManager;
				std::unique_ptr<Fox::Graphics::RHI> currentRHI;

				Fox::Core::Connection keyReleasedBind;

				bool isFullscreen = false;
				bool running = false;
			};

		}

	}

}
