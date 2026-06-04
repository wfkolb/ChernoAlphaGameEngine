# 01 — Bootstrap: `IGame` and `Application`

Every game built on the engine implements two things: an `IGame` subclass that contains the game's logic, and a `WinMain` that hands it to `Application`.

## The `IGame` interface

```cpp
// src/app/public/app/IGame.h
class IGame {
public:
    virtual ~IGame() = default;

    virtual void onInit(GameContext& ctx)                  = 0;
    virtual void onGameTick(GameContext& ctx, float dt)    = 0;
    virtual void onRenderTick(GameContext& ctx, float dt)  = 0;
    virtual void onShutdown(GameContext& ctx)              = 0;
    virtual void onDebugUI(GameContext& ctx) {}  // default no-op; devrel only
};
```

| Hook | When it runs | Typical use |
|------|-------------|-------------|
| `onInit` | Once, after engine systems are ready | Register archetypes, subscribe to events, load config |
| `onGameTick` | Every fixed 64 Hz server tick | Gameplay logic, system dispatch |
| `onRenderTick` | Every variable render frame | Interpolation, camera update, UI |
| `onShutdown` | Once, before engine teardown | Flush saves, close network sessions |
| `onDebugUI` | Every render frame, devrel only | ImGui overlays, developer cheats |

## `GameContext`

`GameContext` is passed into every hook. It holds non-owning references to all engine subsystems — no globals, no singletons.

```cpp
struct GameContext {
    engine::core::ecs::World&          world;
    engine::app::SystemScheduler&      scheduler;
    engine::core::input::InputSystem&  inputSystem;
    engine::rendering::RenderSystem&   renderSystem;
    engine::networking::NetworkSystem& networkSystem;
    engine::app::SaveSystem&           saveSystem;
    engine::app::AssetSystem&          assetSystem;
    engine::core::scene::SceneManager& sceneManager;
    engine::core::EventBus&            eventBus;
    engine::physics::PhysicsWorld&     physicsWorld;
    engine::tools::Logger&             logger;
};
```

Take what you need by reference at the start of each hook rather than storing `GameContext` itself — its contents are stable for the duration of a tick but the struct is stack-allocated.

## Implementing `IGame` for the arena shooter

```cpp
// ArenaGame.h
#pragma once
#include <app/IGame.h>
#include "TDMGameMode.h"

class ArenaGame : public engine::app::IGame {
public:
    void onInit(engine::app::GameContext& ctx) override;
    void onGameTick(engine::app::GameContext& ctx, float dt) override;
    void onRenderTick(engine::app::GameContext& ctx, float dt) override;
    void onShutdown(engine::app::GameContext& ctx) override;
    void onDebugUI(engine::app::GameContext& ctx) override;

private:
    TDMGameMode gameMode_;
};
```

```cpp
// ArenaGame.cpp
#include "ArenaGame.h"
#include <core/fps/FpsArchetypes.h>
#include <app/GameLoop.h>

void ArenaGame::onInit(engine::app::GameContext& ctx) {
    // Register FPS entity archetypes (player, weapon, projectile, etc.)
    engine::core::fps::registerFpsArchetypes(ctx.entityFactory);

    // Subscribe to death events for scorekeeping
    ctx.eventBus.subscribe<engine::app::EntityDiedEvent>(
        [this, &ctx](const engine::app::EntityDiedEvent& e) {
            gameMode_.onPlayerDeath(ctx, e.dead, e.killer);
        });
}

void ArenaGame::onGameTick(engine::app::GameContext& ctx, float dt) {
    ctx.scheduler.tickGroup(engine::app::TickGroup::PrePhysics,  dt);
    // PhysicsWorld::step runs here inside GameLoop
    ctx.scheduler.tickGroup(engine::app::TickGroup::PostPhysics, dt);
    ctx.scheduler.tickGroup(engine::app::TickGroup::GameFixed,   dt);
}

void ArenaGame::onRenderTick(engine::app::GameContext& ctx, float dt) {
    ctx.scheduler.tickGroup(engine::app::TickGroup::Render, dt);
}

void ArenaGame::onShutdown(engine::app::GameContext& ctx) {
    ctx.saveSystem.createCheckpoint();
}
```

## `ApplicationDesc` and `WinMain`

```cpp
// main.cpp
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <app/Application.h>
#include "ArenaGame.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    ArenaGame game;

    engine::app::ApplicationDesc desc;
    desc.windowTitle  = "Arena";
    desc.windowWidth  = 1920;
    desc.windowHeight = 1080;
    desc.game         = &game;
    desc.startScene   = "content/maps/arena_01.scene";

    engine::app::Application app(desc);
    app.run();
    return 0;
}
```

`Application::run()` blocks until the window closes or `onShutdown` returns. Internally it owns the fixed game tick loop (64 Hz) and variable render loop, calling the appropriate `IGame` hooks at each stage.

## `SystemScheduler` and tick groups

Rather than writing all logic directly in `onGameTick`, register systems against named tick groups. The scheduler dispatches them in order within each group.

```cpp
void ArenaGame::onInit(engine::app::GameContext& ctx) {
    // Movement reads input and sets desired velocity — must be before physics
    ctx.scheduler.registerSystem(engine::app::TickGroup::PrePhysics,
        [&ctx](float dt) { movementSystem(ctx, dt); });

    // Sync physics results back to Transform components — must be after physics
    ctx.scheduler.registerSystem(engine::app::TickGroup::PostPhysics,
        [&ctx](float dt) { physicsToTransformSync(ctx, dt); });

    // Game logic that depends on final positions
    ctx.scheduler.registerSystem(engine::app::TickGroup::GameFixed,
        [&ctx](float dt) { lifetimeSystem(ctx, dt); });
}
```

Tick group execution order: `PrePhysics` → *(physics step)* → `PostPhysics` → `GameFixed` → `Network` → `Render`.

## Build targets

Add your game as a new executable in CMake, linking against `engine::app`:

```cmake
add_executable(Arena WIN32
    src/main.cpp
    src/ArenaGame.cpp
    src/TDMGameMode.cpp
)
target_link_libraries(Arena PRIVATE engine::app)
target_compile_features(Arena PRIVATE cxx_std_20)
engine_add_warnings(Arena)
```

## Next

[02 — Game Mode](02_game_mode.md): Implementing `IGameMode` for team deathmatch rules.
