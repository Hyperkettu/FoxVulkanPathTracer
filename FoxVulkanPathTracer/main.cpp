#include "FoxRenderer.h"

#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
    // Writes what it is given to several places: the console, a log file, the editor's Log window
    class TeeBuffer : public std::streambuf {
    public:
        explicit TeeBuffer(std::vector<std::streambuf*> targets) : targets(std::move(targets)) {}

    protected:
        int overflow(int c) override {
            if (c == traits_type::eof()) {
                return traits_type::not_eof(c);
            }
            const auto ch = traits_type::to_char_type(c);
            for (std::streambuf* target : targets) target->sputc(ch);
            return c;
        }
        std::streamsize xsputn(const char* text, std::streamsize count) override {
            for (std::streambuf* target : targets) target->sputn(text, count);
            return count;
        }
        int sync() override {
            int result = 0;
            for (std::streambuf* target : targets) result |= target->pubsync();
            return result == 0 ? 0 : -1;
        }

    private:
        std::vector<std::streambuf*> targets;
    };

    // What the engine prints, also in a log file (written as it goes: there after a crash too), and in the editor
    // also on the console and in its Log window. Never closed: std::cout may still write while the program exits.
    void LogTo(const char* file, bool console, bool editorWindow) {
        auto* log = new std::ofstream(file);
        std::vector<std::streambuf*> out, err;
        if (*log) {
            out.push_back(log->rdbuf());
            err.push_back(log->rdbuf());
        }
        if (console) {
            out.push_back(std::cout.rdbuf());
            err.push_back(std::cerr.rdbuf());
        }
        if (editorWindow) {
            out.push_back(Fox::Editor::Log::Capture());
            err.push_back(Fox::Editor::Log::Capture());
        }
        if (out.empty()) {
            return;
        }
        std::cout.rdbuf(new TeeBuffer(out));
        std::cerr.rdbuf(new TeeBuffer(err));
        std::cout.setf(std::ios::unitbuf);
    }
}

int main(int argc, char* argv[]) {

    // A published game (Game.json beside the executable): its name, and its output in Game.log only. The editor: its
    // output in Editor.log (in the working directory, the project's folder) and its Log window.
    //
    // No console window of its own: one Windows opened for it (started from Explorer or Visual Studio) is closed. One
    // it was started from (a terminal) stays and gets the output too; FOX_CONSOLE=1 keeps it in any case.
    const Fox::Platform::GameConfig& game = Fox::Platform::GetGameConfig();
    bool console = true;
#ifdef _WIN32
    DWORD processes[2];
    const bool ownConsole = GetConsoleProcessList(processes, 2) == 1; // This process alone on it: opened for it
    if (game.enabled || (ownConsole && !SDL_getenv("FOX_CONSOLE"))) {
        FreeConsole();
        console = false;
    }
#endif
    if (game.enabled) {
        LogTo("Game.log", false, false);
    }
    else {
        LogTo("Editor.log", console, true);
    }

    Fox::Platform::Vulkan::Application app(game.enabled ? game.name : std::string("Fox Renderer"), 1280, 840);

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
