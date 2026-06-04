#pragma once
#include "app/ApplicationDesc.h"
#include "app/GameContext.h"
#include "app/GameLoop.h"
#include "app/SystemScheduler.h"
#include <core/diag/Assert.h>
#include <memory>

namespace engine::app {

class Engine;

class Application {
public:
    ENGINE_NO_COPY(Application);
    ENGINE_NO_MOVE(Application);

    Application();
    ~Application();

    bool init(const ApplicationDesc& desc);
    void run();
    void shutdown();

private:
    std::unique_ptr<Engine> engine_;
    SystemScheduler         scheduler_;
    GameLoop                gameLoop_;
    GameContext             context_;
    IGame*                  game_        = nullptr;
    bool                    initialized_ = false;
};

} // namespace engine::app
