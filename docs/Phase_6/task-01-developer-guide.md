# Game Developer Getting Started Guide

**Phase 6 — Engine Version 0.6.x**
**Audience:** Game developers building an FPS (or similar) title on top of the engine.
**Prerequisite reading:** None — this is your entry point. Cross-references to other Phase 6 docs are called out inline.

---

## Table of Contents

1. [Engine Architecture Overview](#1-engine-architecture-overview)
2. [Project Setup](#2-project-setup)
3. [The IGame Interface](#3-the-igame-interface)
4. [Registering ECS Components and Systems](#4-registering-ecs-components-and-systems)
5. [Tick Lifecycle](#5-tick-lifecycle)
6. [FPS-Specific Bootstrapping](#6-fps-specific-bootstrapping)
7. [Cross-Reference: Other Phase 6 Documents](#7-cross-reference-other-phase-6-documents)

---

## 1. Engine Architecture Overview

The engine is organized into four static libraries. A game project links against whichever it needs and builds a thin executable shell on top.

```
┌─────────────────────────────────────────────┐
│                   GAME.exe                  │
│  (IGame subclass, game-specific ECS systems │
│   components, game modes, assets)           │
├──────────────┬──────────────┬───────────────┤
│ engine::core │engine::render│engine::network│
│              │  ing         │  ing          │
│ math         │ DX12 backend │ UDP transport │
│ ECS          │ FrameGraph   │ reliability   │
│ input        │ MeshManager  │ snapshot sync │
│ logging      │ shader cache │ lag comp.     │
├──────────────┴──────────────┴───────────────┤
│            engine::tools  (opt.)            │
│  asset pipeline, offline compiler, editor  │
└─────────────────────────────────────────────┘
```

### What the Engine Provides

| Domain | Engine responsibility |
|---|---|
| Math | `Vec2/3/4`, `Mat3/4`, `Quat`, `Transform`; row-major, right-handed, Y-up |
| ECS | Archetype storage, system scheduler, query API |
| Input | Action/binding abstraction, raw mouse, gamepad XInput |
| Rendering | DX12 device/swapchain, FrameGraph, mesh/texture upload, built-in passes |
| Networking | UDP socket management, reliability layer, world-state snapshot |
| Physics | Collision, rigidbody integration (see Physics/Scene lead doc) |
| Tools | Offline asset compilation, `.assetpack` format, in-process editor hooks |

### What the Game Provides

- Concrete `IGame` subclass — the game's "main" class
- Game-specific ECS components (health, ammo, inventory, etc.)
- Game-specific ECS systems (movement controller, weapon logic, round manager, etc.)
- Game mode implementations (`IGameMode` — see `task-07-gameplay-loop.md`)
- Content: map files, audio, textures, config
- `input.toml` bindings (see `task-02-input-system.md`)

The split is intentional: the engine owns all cross-cutting infrastructure; the game owns all domain logic. Engine systems are data-oriented and operate on ECS components. The game registers its own components and systems through the engine's registration API before the main loop starts.

---

## 2. Project Setup

### 2.1 Prerequisites

- Visual Studio 2022 (MSVC v143) or the standalone Build Tools
- CMake 3.28+
- Windows 10 SDK 10.0.22621 or newer
- The engine installed (or available as a git submodule) at a known path

### 2.2 Recommended Directory Structure

```
mygame/
├── CMakeLists.txt
├── cmake/
│   └── FindEngine.cmake        # or engine is a submodule
├── content/
│   ├── maps/
│   ├── textures/
│   ├── audio/
│   └── shaders/                # game-side HLSL only
├── config/
│   ├── input.toml
│   ├── server.toml
│   └── game.toml
├── src/
│   ├── main.cpp
│   ├── MyGame.h / MyGame.cpp
│   ├── components/
│   │   ├── HealthComponent.h
│   │   └── WeaponComponent.h
│   ├── systems/
│   │   ├── PlayerMovementSystem.h
│   │   └── WeaponSystem.h
│   └── gamemodes/
│       ├── TeamDeathmatchMode.h
│       └── BombDefusalMode.h
└── tools/
    └── asset_cook.bat          # invokes engine::tools asset pipeline
```

### 2.3 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.28)
project(MyGame CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Locate engine -----------------------------------------------------------
# Option A: engine as a sibling directory already built
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/../engine/install")

# Option B: engine as submodule, add_subdirectory approach
# add_subdirectory(engine)

find_package(Engine REQUIRED)   # exposes engine::core, engine::rendering, etc.

# --- Game executable ---------------------------------------------------------
add_executable(MyGame WIN32
    src/main.cpp
    src/MyGame.cpp
    src/systems/PlayerMovementSystem.cpp
    src/systems/WeaponSystem.cpp
    src/gamemodes/TeamDeathmatchMode.cpp
    src/gamemodes/BombDefusalMode.cpp
)

target_link_libraries(MyGame PRIVATE
    engine::core
    engine::rendering
    engine::networking
)

# Copy content to build directory
add_custom_command(TARGET MyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/content"
        "$<TARGET_FILE_DIR:MyGame>/content"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/config"
        "$<TARGET_FILE_DIR:MyGame>/config"
)
```

### 2.4 main.cpp

The entry point constructs the engine `Application` object, hands it a game factory, and calls `run()`. The engine owns the message loop.

```cpp
// main.cpp
#include <engine/core/Application.h>
#include "MyGame.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    engine::ApplicationDesc desc{};
    desc.appName        = L"MyGame";
    desc.windowWidth    = 1920;
    desc.windowHeight   = 1080;
    desc.configPath     = "config/game.toml";
    desc.gameFactory    = []() -> std::unique_ptr<engine::IGame> {
        return std::make_unique<MyGame>();
    };

    engine::Application app(desc);
    return app.run();   // blocks until window is closed
}
```

The `Application` class handles:
- Win32 window creation and the Windows message loop
- DX12 device/swapchain initialization
- Engine subsystem startup in dependency order
- Calling `IGame::onInit()`, then entering the main loop
- Calling `IGame::onShutdown()` on exit

---

## 3. The IGame Interface

`IGame` is the primary extension point. Declare one concrete subclass per game.

```cpp
// engine/core/IGame.h (engine-provided, do not modify)
namespace engine {

class IGame
{
public:
    virtual ~IGame() = default;

    // Called once after all engine subsystems are up.
    // Register components, systems, game modes here.
    virtual void onInit(GameContext& ctx) = 0;

    // Called each variable-rate render tick (before rendering).
    // Interpolation, camera updates, HUD logic live here.
    virtual void onRenderTick(GameContext& ctx, float deltaSec) {}

    // Called each fixed-rate game tick (64 Hz by default).
    // Deterministic simulation logic lives here.
    virtual void onGameTick(GameContext& ctx, float fixedDeltaSec) {}

    // Called once during shutdown, after the main loop exits.
    virtual void onShutdown(GameContext& ctx) {}

    // Called when the engine wants a debug overlay string (ImGui, etc.)
    virtual void onDebugUI(GameContext& ctx) {}
};

} // namespace engine
```

### 3.1 GameContext

`GameContext` is passed into every `IGame` callback. It is a non-owning view of the running engine state:

```cpp
struct GameContext
{
    World&           world;       // ECS world (entities + components)
    SystemScheduler& scheduler;   // register/remove systems at runtime
    InputSystem&     input;       // query actions, re-bind keys
    RenderSystem&    renderer;    // submit draw commands, manage cameras
    NetworkSystem&   network;     // send/receive, connect/disconnect
    SaveSystem&      save;        // profiles, match records, checkpoints
    AssetSystem&     assets;      // load meshes, textures, sounds
    Logger&          log;
};
```

### 3.2 Minimal IGame Implementation

```cpp
// MyGame.h
#pragma once
#include <engine/core/IGame.h>

class MyGame : public engine::IGame
{
public:
    void onInit(engine::GameContext& ctx) override;
    void onGameTick(engine::GameContext& ctx, float dt) override;
    void onRenderTick(engine::GameContext& ctx, float dt) override;
    void onShutdown(engine::GameContext& ctx) override;
};
```

```cpp
// MyGame.cpp
#include "MyGame.h"
#include "components/HealthComponent.h"
#include "components/WeaponComponent.h"
#include "systems/PlayerMovementSystem.h"
#include "systems/WeaponSystem.h"
#include "gamemodes/TeamDeathmatchMode.h"

void MyGame::onInit(engine::GameContext& ctx)
{
    // 1. Register game-specific ECS components
    ctx.world.registerComponent<HealthComponent>();
    ctx.world.registerComponent<WeaponComponent>();

    // 2. Register game-specific ECS systems (ordered)
    ctx.scheduler.addSystem<PlayerMovementSystem>(engine::TickGroup::GameFixed);
    ctx.scheduler.addSystem<WeaponSystem>(engine::TickGroup::GameFixed);

    // 3. Register game mode
    ctx.network.setGameMode(std::make_unique<TeamDeathmatchMode>());

    // 4. Load initial scene (async; callback fires when loaded)
    ctx.assets.loadScene("content/maps/de_harbor.ascene", [&ctx](engine::SceneHandle scene) {
        ctx.world.instantiateScene(scene);
    });

    // 5. Configure input bindings (supplements input.toml)
    ctx.input.bindAction("Fire",   engine::InputDevice::Mouse,   engine::MouseButton::Left);
    ctx.input.bindAction("ADS",    engine::InputDevice::Mouse,   engine::MouseButton::Right);
    ctx.input.bindAction("Jump",   engine::InputDevice::Keyboard, engine::Key::Space);
    ctx.input.bindAction("Crouch", engine::InputDevice::Keyboard, engine::Key::LeftControl);
}

void MyGame::onGameTick(engine::GameContext& ctx, float dt)
{
    // Game-tick logic not covered by registered systems goes here.
    // Most logic should live in registered systems — keep this lean.
}

void MyGame::onRenderTick(engine::GameContext& ctx, float dt)
{
    // Camera interpolation, HUD, etc.
}

void MyGame::onShutdown(engine::GameContext& ctx)
{
    ctx.save.flushAll();   // ensure pending save writes are flushed
}
```

---

## 4. Registering ECS Components and Systems

### 4.1 Components

Components are plain data structs. They must be trivially copyable (no virtual functions, no owning smart pointers) so the ECS archetype storage can memmove them.

```cpp
// components/HealthComponent.h
#pragma once
#include <cstdint>

struct HealthComponent
{
    float    current   = 100.f;
    float    maximum   = 100.f;
    float    armorAbs  = 0.f;    // flat damage reduction
    uint32_t teamIndex = 0;
};
```

Registration before first use:

```cpp
ctx.world.registerComponent<HealthComponent>();
```

Registration is idempotent; calling it twice for the same type is a no-op.

### 4.2 Systems

A system is a class with an `update(World& world, float dt)` method. Systems declare which components they read/write via a static `queryDesc()` so the scheduler can detect data hazards and parallelize safely.

```cpp
// systems/WeaponSystem.h
#pragma once
#include <engine/core/System.h>
#include "components/WeaponComponent.h"
#include "components/HealthComponent.h"

class WeaponSystem : public engine::ISystem
{
public:
    static engine::QueryDesc queryDesc()
    {
        return engine::QueryDesc{}
            .readWrite<WeaponComponent>()
            .read<engine::TransformComponent>()
            .read<engine::InputReceiverComponent>();
    }

    void update(engine::World& world, float dt) override;
};
```

```cpp
// systems/WeaponSystem.cpp
#include "WeaponSystem.h"

void WeaponSystem::update(engine::World& world, float dt)
{
    world.query<WeaponComponent,
                engine::TransformComponent,
                engine::InputReceiverComponent>(
        [dt](engine::EntityId entity,
             WeaponComponent& weapon,
             const engine::TransformComponent& xform,
             const engine::InputReceiverComponent& input)
        {
            if (input.isActionPressed("Fire") && weapon.cooldownRemaining <= 0.f)
            {
                weapon.cooldownRemaining = 1.f / weapon.fireRateHz;
                // fire logic ...
            }
            weapon.cooldownRemaining -= dt;
        });
}
```

### 4.3 Tick Groups

Systems are assigned to a tick group at registration time:

| Group | Rate | Purpose |
|---|---|---|
| `TickGroup::GameFixed` | 64 Hz | Deterministic simulation, physics-adjacent logic |
| `TickGroup::Render` | Uncapped / vsync | Interpolation, particle updates, camera |
| `TickGroup::Network` | ~20 Hz | Snapshot generation/consumption |
| `TickGroup::PrePhysics` | 64 Hz, before physics | Apply forces, pre-sim queries |
| `TickGroup::PostPhysics` | 64 Hz, after physics | Collision response, damage |

```cpp
ctx.scheduler.addSystem<PlayerMovementSystem>(engine::TickGroup::PrePhysics);
ctx.scheduler.addSystem<WeaponSystem>(engine::TickGroup::PostPhysics);
```

---

## 5. Tick Lifecycle

The engine runs three interleaved loops. Understanding this model is essential for placing logic in the right tick group.

```
Wall clock
  │
  ├── Fixed tick accumulator (64 Hz = 15.625 ms / tick)
  │     ├─ Input::collectFrame()          // snapshot raw input
  │     ├─ NetworkSystem::receive()       // process inbound packets
  │     ├─ [PrePhysics systems]
  │     ├─ PhysicsSystem::step(fixedDt)   // integrate + collide
  │     ├─ [PostPhysics systems]
  │     ├─ IGame::onGameTick()
  │     └─ NetworkSystem::send()          // outbound at ~20 Hz (every 3 game ticks)
  │
  └── Render tick (every frame, variable dt)
        ├─ [Render-group systems]
        ├─ IGame::onRenderTick()
        ├─ RenderSystem::buildFrameGraph()
        └─ RenderSystem::submit() → DX12 present
```

### 5.1 Fixed vs. Variable Logic

- **Deterministic/networked state** (positions, health, ammo, round timer) must live in `GameFixed` group systems. These run at a constant rate so server and client diverge predictably — the prediction-reconciliation system depends on this.
- **Visual/aesthetic state** (particle lifetimes, camera smoothing, UI animations) belongs in `Render` group systems. Never read predicted game-state from here and write it back; treat it as read-only from the simulation perspective.
- The render tick reads a **snapshot of the last completed game tick** (double-buffered) to avoid partial-tick reads during a game-tick in progress.

### 5.2 Frame Pacing

Frame pacing is controlled in `config/game.toml`:

```toml
[engine.timing]
fixed_tick_hz      = 64        # server tick rate; must match server
max_fixed_ticks_per_frame = 4  # safety clamp; prevents spiral of death
vsync              = true
max_render_fps     = 240       # only respected when vsync = false
```

---

## 6. FPS-Specific Bootstrapping

This section walks through the minimal sequence to get a playable FPS running, covering both a dedicated-server launch and a listen-server (host + play) launch.

### 6.1 Dedicated Server Launch

The server runs headless (no window, no DX12 device). The engine detects this via the `--server` command-line flag and skips rendering subsystem startup entirely.

```cpp
void MyGame::onInit(engine::GameContext& ctx)
{
    if (ctx.network.isServer())
    {
        // Server-only init: load map, set game mode, open listener
        ctx.assets.loadScene("content/maps/de_harbor.ascene", [&ctx](auto scene) {
            ctx.world.instantiateScene(scene);
        });
        ctx.network.setGameMode(std::make_unique<BombDefusalMode>());
        ctx.network.listenOnPort(7777);
        ctx.log.info("Server ready on :7777");
    }
    else
    {
        // Client-only init: load HUD, start connecting
        loadClientHUD(ctx);
        ctx.network.connectTo("127.0.0.1", 7777);
    }
}
```

Launch commands:

```bat
rem Dedicated server
MyGame.exe --server --map de_harbor --mode bomb --port 7777

rem Client
MyGame.exe --connect 127.0.0.1:7777
```

### 6.2 Listen Server (Local Game / LAN)

A listen server runs both the server game loop and the client renderer in the same process. Use this for singleplayer tests or LAN play.

```cpp
void MyGame::onInit(engine::GameContext& ctx)
{
    ctx.network.startListenServer(7777);
    ctx.assets.loadScene("content/maps/aim_range.ascene", [&ctx](auto scene) {
        ctx.world.instantiateScene(scene);
        // Spawn a local player immediately
        engine::EntityId localPlayer = spawnPlayer(ctx, ctx.network.localPlayerId());
        ctx.network.setLocalPlayerEntity(localPlayer);
    });
    ctx.network.setGameMode(std::make_unique<TeamDeathmatchMode>());
}
```

### 6.3 Spawning a Player Entity

Player spawning is game-side responsibility. The engine provides the `PlayerTag`, `TransformComponent`, `InputReceiverComponent`, and `NetworkedEntityComponent`; the game adds domain components.

```cpp
engine::EntityId spawnPlayer(engine::GameContext& ctx, engine::PlayerId pid)
{
    engine::EntityId e = ctx.world.createEntity();

    // Engine-provided components
    ctx.world.addComponent<engine::TransformComponent>(e, spawnTransformFor(pid));
    ctx.world.addComponent<engine::NetworkedEntityComponent>(e, { .ownerPlayerId = pid });
    ctx.world.addComponent<engine::InputReceiverComponent>(e, { .playerId = pid });
    ctx.world.addComponent<engine::CameraComponent>(e, engine::CameraComponent::defaultFPS());

    // Game-defined components
    ctx.world.addComponent<HealthComponent>(e, { .current = 100.f, .maximum = 100.f });
    ctx.world.addComponent<WeaponComponent>(e, defaultLoadout());

    return e;
}
```

### 6.4 Scene Loading

Scenes are authored in the editor and compiled to `.ascene` binary files by the asset pipeline. Scene load is asynchronous; the engine continues ticking during load. Your `onInit` must tolerate the scene not yet being present for the first few ticks — use the callback pattern shown above rather than assuming the scene is ready synchronously.

For dedicated servers loading large maps, consider blocking until load completes before opening the network listener:

```cpp
auto future = ctx.assets.loadSceneAsync("content/maps/de_harbor.ascene");
future.waitForCompletion();                   // blocks game tick; OK on server
ctx.world.instantiateScene(future.result());
ctx.network.listenOnPort(7777);
```

---

## 7. Cross-Reference: Other Phase 6 Documents

| Document | What it covers | When to read |
|---|---|---|
| `task-02-input-system.md` | InputAction, InputBinding, `input.toml`, networked input frames, mouse sensitivity | Before writing any player-controller code |
| `task-07-gameplay-loop.md` | Server-authoritative loop detail, client-side prediction, lag compensation, IGameMode, WorldState snapshot | Before writing game mode or any simulation system |
| `task-08-save-system.md` | Player profiles, match records, server checkpoints, reconnection, save API | Before writing persistence, stats, or settings code |
| Physics/Scene doc (separate lead) | Collision shapes, rigidbody, character controller, raycasts | Before writing movement or hit detection |

---

*Document maintained by the engine team. File issues against the `engine-phase6` milestone.*
