#include "FoxRenderer.h"

int main(int argc, char* argv[]) {

    Fox::Platform::Vulkan::Application app("Fox Renderer", 1280, 840);

    try {
        if (!app.Initialize()) {
            return EXIT_FAILURE;
        }

        app.Run();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
