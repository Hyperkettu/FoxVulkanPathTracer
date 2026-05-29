#include "FoxRenderer.h"

int main(int argc, char* argv[]) {
   
    Fox::Platform::Vulkan::Application app("Fox Renderer", 1920 * 0.5, 1080 * 0.5);

    if (!app.Initialize()) {
        exit(1);
    }

    app.Run();

    return 0;
}