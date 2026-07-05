#include "FoxRenderer.h"

int main(int argc, char* argv[]) {
   
    Fox::Platform::Vulkan::Application app("Fox Renderer", 0.5 * 2560, 0.5 * 1680);

    if (!app.Initialize()) {
        exit(1);
    }

    app.Run();

    return 0;
}