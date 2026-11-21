#include "FoxRenderer.h"

#include <chrono>

namespace Fox {

	namespace Platform {

		namespace Vulkan {

			Application::~Application() {
				keyReleasedBind.Disconnect();
				currentRHI = nullptr;
				inputManager = nullptr;

				if (window) {
					SDL_DestroyWindow(window);
				}
				SDL_Quit();
			}

			bool Application::Initialize() {
				// The splash screen while everything loads; the main window shows once its first frame is drawn (Run)
				Fox::Platform::Splash::Show("Textures/Fox Engine Logo.png");
				Fox::Platform::Splash::SetStatus("Starting");

				if (SDL_Init(SDL_INIT_VIDEO) != 0) {
					Fox::Platform::Splash::Close();
					std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
					return false;
				}

				window = SDL_CreateWindow(title.c_str(),
					SDL_WINDOWPOS_CENTERED,
					SDL_WINDOWPOS_CENTERED,
					static_cast<int>(width), static_cast<int>(height),
					SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);

				if (!window) {
					Fox::Platform::Splash::Close();
					std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
					return false;
				}

				// Instance extensions SDL needs to create a Vulkan surface for this window
				unsigned int extensionCount = 0;
				if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr)) {
					std::cerr << "SDL_Vulkan_GetInstanceExtensions (count) failed: " << SDL_GetError() << "\n";
					return false;
				}

				Fox::Graphics::RendererConfig config;
				config.instanceExtensions.resize(extensionCount);
				if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, config.instanceExtensions.data())) {
					std::cerr << "SDL_Vulkan_GetInstanceExtensions (data) failed: " << SDL_GetError() << "\n";
					return false;
				}

#ifdef _DEBUG
				config.instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
				config.layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

				config.windowHandle = static_cast<void*>(window);
				config.MAX_FRAMES_IN_FLIGHT = 2u;

				inputManager = std::make_unique<Fox::Input::InputManager>();

				currentRHI = std::make_unique<Fox::Graphics::Vulkan::VulkanRHI>();
				if (!currentRHI->Initialize(config)) {
					Fox::Platform::Splash::Close();
					return false;
				}

				currentRHI->RegisterInput(*inputManager);
				keyReleasedBind = inputManager->OnKeyReleased.ConnectMember(this, &Application::OnKeyReleased);

				return true;
			}

			void Application::OnKeyReleased(SDL_Scancode key) {
				switch (key) {
				case SDL_SCANCODE_T:
					ToggleFullscreen();
					break;
				case SDL_SCANCODE_ESCAPE:
					running = false;
					break;
				default:
					break;
				}
			}

			void Application::ToggleFullscreen() {
				isFullscreen = !isFullscreen;

				if (isFullscreen) {
					// Exclusive fullscreen at the desktop's resolution and refresh rate. Frames then go straight
					// to the display: in a borderless window they pass through the desktop compositor, which
					// on Windows latches Vulkan frames irregularly (every few frames waits an extra refresh,
					// even when the GPU has time to spare).
					SDL_DisplayMode desktopMode{};
					if (SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(window), &desktopMode) == 0) {
						SDL_SetWindowDisplayMode(window, &desktopMode);
					}
					SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
				}
				else {
					SDL_SetWindowFullscreen(window, 0);
				}

				int w = 0, h = 0;
				SDL_Vulkan_GetDrawableSize(window, &w, &h);
				width = static_cast<uint32_t>(w);
				height = static_cast<uint32_t>(h);

				currentRHI->Resize(width, height);
			}

			void Application::Run() {
				using Clock = std::chrono::steady_clock;

				running = true;
				inputManager->ResetDeltas();

				auto previousTime = Clock::now();

				// Loaded: the window, and once its first frame is drawn, the splash screen goes
				Fox::Platform::Splash::SetStatus("Ready");
				SDL_ShowWindow(window);
				SDL_RaiseWindow(window);
				bool splashOpen = true;

				while (running) {
					SDL_Event e;
					while (SDL_PollEvent(&e)) {
						if (e.type == SDL_QUIT) {
							running = false;
						}

						if (!currentRHI->HandleEvent(e)) {
							inputManager->ProcessEvent(e);
						}
					}

					const auto now = Clock::now();
					// Clamp so a long stall (e.g. window drag) does not teleport the camera
					const float deltaTime = std::min(std::chrono::duration<float>(now - previousTime).count(), 0.1f);
					previousTime = now;

					// Presentation is vsynced (FIFO), which paces the loop - no manual sleep needed
					currentRHI->Render(deltaTime);
					if (splashOpen) {
						splashOpen = false;
						Fox::Platform::Splash::Close();
						previousTime = Clock::now(); // The fade out is not a frame's time
					}
				}
			}
		}
	}
}
