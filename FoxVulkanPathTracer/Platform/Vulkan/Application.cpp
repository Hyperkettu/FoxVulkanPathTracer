#include "FoxRenderer.h"

namespace Fox {

	namespace Platform {
	
		namespace Vulkan {
			
			Application::Application() {}

			Application::~Application() {
                std::cout << "Destroy application" << std::endl;
                currentRHI = nullptr;
                SDL_DestroyWindow(window);
                SDL_Quit();
            }

			int32_t Application::Initialize() {
                // Initialize SDL with video and Vulkan
                if (SDL_Init(SDL_INIT_VIDEO) != 0) {
                    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
                    return EXIT_FAILURE;
                }

                window = SDL_CreateWindow(title.c_str(),
                    SDL_WINDOWPOS_CENTERED,
                    SDL_WINDOWPOS_CENTERED,
                    width, height,
                    SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);

                if (!window) {
                    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
                    SDL_Quit();
                    return EXIT_FAILURE;
                }

                // Get required Vulkan extensions from SDL
                unsigned int extensionCount = 0;
                if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr)) {
                    std::cerr << "SDL_Vulkan_GetInstanceExtensions (count) failed\n";
                    return EXIT_FAILURE;
                }

                std::vector<const char*> extensions(extensionCount);
                if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensions.data())) {
                    std::cerr << "SDL_Vulkan_GetInstanceExtensions (data) failed\n";
                    return EXIT_FAILURE;
                }

                // for mesh shaders 
                extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
                extensionCount++;

#if _DEBUG
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                extensionCount++;
#endif

                std::vector<const char*> layers;
#if _DEBUG
                layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

                inputManager = std::make_unique<Fox::Input::InputManager>();

                Fox::Graphics::RendererConfig config;
                config.extensionCount = extensionCount;
                config.instanceExtensions = extensions;
                config.layerCount = static_cast<uint32_t>(layers.size());
                config.layers = layers;
                config.windowHandle = static_cast<void*>(window);
                config.MAX_FRAMES_IN_FLIGHT = 2u;

                currentRHI = std::make_unique<Fox::Graphics::Vulkan::VulkanRHI>();
                int32_t vulkanInitialized = currentRHI->Initialize(config);

                currentRHI->RegisterInput(*inputManager.get());

                if (!vulkanInitialized) {
                    return vulkanInitialized;
                }

                return 1;
			}

            void Application::ToggleScreen(SDL_Scancode key) {
                if (key == SDL_SCANCODE_T) {
                    ToggleFullscreen(window, isFullscreen);
                    int w, h;
                    SDL_GetWindowSize(window, &w, &h);
                    width = static_cast<uint32_t>(w);
                    height = static_cast<uint32_t>(h);

                    currentRHI->Resize(width, height);
                }

            }
		
			void Application::Run() {
                SDL_Event e;
                bool running = true;

                fullscreenToggleBind = inputManager->OnKeyReleased.ConnectMember(this, &Application::ToggleScreen);

                inputManager->ResetDeltas();
                while (running) {
                    while (SDL_PollEvent(&e)) {
                        if (e.type == SDL_QUIT) {
                            running = false;
                        }

                        inputManager->ProcessEvent(e);
                    }
                    currentRHI->Render();
                    SDL_Delay(16);
                }
			}
		}
	}
}