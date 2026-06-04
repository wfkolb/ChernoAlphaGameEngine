#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "app/Application.h"
#include "app/ApplicationDesc.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    engine::app::ApplicationDesc desc;
    desc.windowTitle  = L"Engine v0.1";
    desc.windowWidth  = 1280;
    desc.windowHeight = 720;
    desc.vsync        = true;
    desc.game         = nullptr;

    engine::app::Application app;
    if (!app.init(desc)) return 1;
    app.run();
    app.shutdown();
    return 0;
}
