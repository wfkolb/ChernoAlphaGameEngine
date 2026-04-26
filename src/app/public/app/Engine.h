#pragma once
#include "rendering/Window.h"
#include "rendering/GpuDevice.h"
#include "rendering/FrameGraph.h"
#include "core/ecs/World.h"
#include "core/EventBus.h"
#include <memory>
#include <functional>

namespace engine::app {

struct EngineConfig {
    uint32_t       windowWidth  = 1280;
    uint32_t       windowHeight = 720;
    const wchar_t* windowTitle  = L"Engine";
    bool           vsync        = true;
};

// Owns all engine subsystems. Call init(), run(), shutdown() in order.
class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    bool init(const EngineConfig& cfg = {});
    void run(std::function<void(core::ecs::World&, rendering::FrameGraph&)> onTick);
    void shutdown();

    // Accessors — valid after init().
    core::ecs::World&      world()     { return *world_; }
    core::EventBus&        eventBus()  { return *eventBus_; }
    rendering::Window&     window()    { return window_->w; }
    rendering::GpuDevice&  device()    { return device_->d; }
    rendering::FrameGraph& frameGraph(){ return *frameGraph_; }

private:
    // Window and GpuDevice are move-only value types returned by ::create().
    // We wrap them in unique_ptr<> via a holder so they can be late-initialised
    // and individually reset during shutdown without triggering copies.
    struct WindowHolder     { rendering::Window     w; };
    struct GpuDeviceHolder  { rendering::GpuDevice  d; };

    // WinsockGuard is NO_MOVE — kept in a holder so the guard itself never moves.
    struct WinsockGuardHolder;

    std::unique_ptr<core::ecs::World>      world_;
    std::unique_ptr<core::EventBus>        eventBus_;
    std::unique_ptr<WindowHolder>          window_;
    std::unique_ptr<GpuDeviceHolder>       device_;
    std::unique_ptr<rendering::FrameGraph> frameGraph_;
    std::unique_ptr<WinsockGuardHolder>    winsockGuard_;

    bool initialized_ = false;

    // Convenience forwarders so call-sites read naturally.
    rendering::Window&    win() { return window_->w; }
    rendering::GpuDevice& dev() { return device_->d; }
};

} // namespace engine::app
