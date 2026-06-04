#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#ifdef ENGINE_DEVREL

#include "editor/EditorApp.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    engine::editor::EditorApp app;
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
