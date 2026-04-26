#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "app/Engine.h"
#include "core/log.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    engine::app::Engine engine;

    engine::app::EngineConfig cfg;
    cfg.windowWidth  = 1280;
    cfg.windowHeight = 720;
    cfg.windowTitle  = L"Engine v0.1";
    cfg.vsync        = true;

    if (!engine.init(cfg)) return 1;

    engine.run([](engine::core::ecs::World& /*world*/,
                  engine::rendering::FrameGraph& /*fg*/) {
        // Game loop placeholder — real game systems go here.
    });

    engine.shutdown();
    return 0;
}
