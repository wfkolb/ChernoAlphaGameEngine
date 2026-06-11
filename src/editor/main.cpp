#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#ifdef ENGINE_DEVREL

#include "editor/EditorApp.h"
#include <filesystem>
#include <shellapi.h>
#include <string_view>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Parse --project <path> from the Unicode command line.
    std::filesystem::path projectRoot;
    {
        int    argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc - 1; ++i) {
                if (std::wstring_view(argv[i]) == L"--project") {
                    projectRoot = argv[i + 1];
                    break;
                }
            }
            LocalFree(argv);
        }
    }

    // If --project was not supplied, search for project.toml:
    //   1. Current working directory
    //   2. Walk up from the exe directory (handles deep build trees like build/devrel/src/...)
    if (projectRoot.empty()) {
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!ec && std::filesystem::exists(cwd / "project.toml", ec))
            projectRoot = cwd;
    }
    if (projectRoot.empty()) {
        wchar_t exeBuf[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exeBuf, MAX_PATH)) {
            std::error_code ec;
            std::filesystem::path dir = std::filesystem::path(exeBuf).parent_path();
            for (int depth = 0; depth < 10 && !dir.empty(); ++depth) {
                if (std::filesystem::exists(dir / "project.toml", ec)) {
                    projectRoot = dir;
                    break;
                }
                std::filesystem::path parent = dir.parent_path();
                if (parent == dir) break;
                dir = std::move(parent);
            }
        }
    }

    engine::editor::EditorApp app;
    if (!projectRoot.empty())
        app.setProjectRoot(projectRoot);

    if (!app.init()) {
        MessageBoxW(nullptr,
                    L"Failed to initialize the editor (no DX12 device / headless?).",
                    L"EngineEditor", MB_OK | MB_ICONERROR);
        return 1;
    }
    app.run();
    app.shutdown();
    return 0;
}

#else

// Editor is a DevRel-only tool; in non-DevRel configs produce a trivial binary
// so the target still links. Must match the WIN32 subsystem entry point.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return 0; }

#endif // ENGINE_DEVREL
