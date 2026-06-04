#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "app/Application.h"
#include "app/IGame.h"
#include "app/Engine.h"
#include <algorithm>
#include <chrono>

namespace engine::app {

Application::Application() : engine_(std::make_unique<Engine>()) {}

Application::~Application() {
    if (initialized_) shutdown();
}

bool Application::init(const ApplicationDesc& desc) {
    EngineConfig cfg;
    cfg.windowWidth  = desc.windowWidth;
    cfg.windowHeight = desc.windowHeight;
    cfg.windowTitle  = desc.windowTitle.c_str();
    cfg.vsync        = desc.vsync;

    if (!engine_->init(cfg)) return false;

    context_.world           = &engine_->world();
    context_.systemScheduler = &scheduler_;

    GameLoop::Desc loopDesc{};
    loopDesc.scheduler = &scheduler_;
    gameLoop_.init(loopDesc);

    game_ = desc.game;
    if (game_) game_->onInit(context_);

    initialized_ = true;
    return true;
}

void Application::run() {
    using Clock   = std::chrono::high_resolution_clock;
    using Seconds = std::chrono::duration<float>;

    constexpr float kFixedDt      = 1.0f / 64.0f;
    constexpr float kMaxFrameTime = 0.25f;
    float           accumulator   = 0.0f;
    auto            prevTime      = Clock::now();

    engine_->run([&](core::ecs::World& /*world*/, rendering::FrameGraph& /*fg*/) {
        auto  now     = Clock::now();
        float frameDt = std::min(Seconds(now - prevTime).count(), kMaxFrameTime);
        prevTime      = now;

        accumulator += frameDt;

        while (accumulator >= kFixedDt) {
            gameLoop_.serverTick(context_, kFixedDt);
            if (game_) game_->onGameTick(context_, kFixedDt);
            accumulator -= kFixedDt;
        }

        scheduler_.tickGroup(TickGroup::Network, frameDt);
        scheduler_.tickGroup(TickGroup::Render,  frameDt);
        if (game_) game_->onRenderTick(context_, frameDt);

#ifdef ENGINE_DEVREL
        if (game_) game_->onDebugUI(context_);
#endif
    });
}

void Application::shutdown() {
    if (!initialized_) return;
    if (game_) game_->onShutdown(context_);
    engine_->shutdown();
    initialized_ = false;
}

} // namespace engine::app
