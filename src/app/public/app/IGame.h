#pragma once

namespace engine::app {

struct GameContext;

class IGame {
public:
    virtual ~IGame() = default;
    virtual void onInit(GameContext& ctx)               = 0;
    virtual void onGameTick(GameContext& ctx, float dt)  = 0;
    virtual void onRenderTick(GameContext& ctx, float dt) = 0;
    virtual void onShutdown(GameContext& ctx)            = 0;
    virtual void onDebugUI(GameContext& /*ctx*/)         {}
};

} // namespace engine::app
