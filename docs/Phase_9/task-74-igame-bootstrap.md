# Task #74 — IGame Bootstrap + Release Wiring

**Phase 9 — app — Version 0.9.x**
**Audience:** Team Lead, Gameplay Lead
**Depends on:** #66 (camera), #68 (spawn system), #72 (trigger volumes)
**Unblocks:** Actual shippable game binary

---

## 1. Goal

The release build of `engine.exe` currently runs with `desc.game = nullptr` — it is an engine shell with no game logic. This task creates a concrete `FpsGame : IGame` implementation and wires it into `WinMain.cpp` so that a release build boots into a real game with a start scene, a game mode, and working player spawning.

---

## 2. Current State

- `WinMain.cpp`: `desc.game = nullptr; desc.startScenePath = "";` — no game, no start scene.
- `IGame` and `IGameMode` interfaces exist and are correct.
- No `src/game/` directory exists.
- No concrete `IGameMode` implementation exists anywhere.

---

## 3. New Module: `engine::game`

Create a `src/game/` directory with its own CMakeLists. This is the game-specific layer that links the engine and provides the concrete implementations:

```
src/game/
├── CMakeLists.txt
├── FpsGame.h / .cpp          — IGame implementation
├── DeathMatchMode.h / .cpp   — IGameMode for team deathmatch
└── public/
    └── game/
        └── FpsGame.h
```

**`src/game/CMakeLists.txt`:**
```cmake
add_library(engine_game STATIC
    FpsGame.cpp
    DeathMatchMode.cpp
)
target_link_libraries(engine_game
    PRIVATE engine::app
    PRIVATE engine::core
    PRIVATE engine::physics
    PRIVATE engine::networking
)
```

`engine.exe` (WinMain.cpp) links `engine_game`. The editor does **not** link `engine_game` — it has no IGame.

---

## 4. FpsGame : IGame

**File:** `src/game/FpsGame.h/.cpp`

```cpp
class FpsGame : public app::IGame {
public:
    void onInit(app::GameContext& ctx) override;
    void onGameTick(app::GameContext& ctx, float dt) override;
    void onRenderTick(app::GameContext& ctx, float dt) override;
    void onShutdown(app::GameContext& ctx) override;
    void onDebugUI(app::GameContext& ctx) override;
};
```

**`onInit()`:**
1. Register FPS archetypes with `EntityFactory` (`registerFpsArchetypes(factory)`).
2. Register `DeathMatchMode` as the active game mode in `GameLoop`.
3. Bind default input actions (`loadBindingsFromToml("config/input.toml")`).
4. Load the start scene (`ctx.sceneManager->load(startScenePath_)`).

**`onGameTick()`:** Stub — game mode logic runs in `GameLoop`; `FpsGame` defers to it.

**`onDebugUI()`:** Show a minimal HUD (scores, round timer) using ImGui. Only compiled in `ENGINE_DEVREL`.

---

## 5. DeathMatchMode : IGameMode

**File:** `src/game/DeathMatchMode.h/.cpp`

Implements the simplest meaningful game mode: kill limit or time limit, team or free-for-all.

```cpp
class DeathMatchMode : public app::IGameMode {
public:
    void onRoundStart(app::GameContext& ctx) override;
    void onRoundTick(app::GameContext& ctx, float dt) override;
    void onPlayerDeath(app::GameContext& ctx, uint32_t victim, uint32_t killer) override;
    bool evaluateWinCondition(app::GameContext& ctx) override;

    // #68 integration:
    ecs::EntityId selectSpawnPoint(uint32_t playerId, uint8_t teamId,
                                   std::span<const ecs::EntityId> available) override;
    // #72 integration:
    void onTriggerEnter(ecs::EntityId trigger, ecs::EntityId entering) override;
};
```

**`selectSpawnPoint()`:** Returns the highest-priority spawn point whose `exclusionRadius` is clear (no player within radius). If all points are occupied, falls back to the first available one.

**`evaluateWinCondition()`:** Returns true when a team or player reaches the configured kill limit (`scoreLimit`, set via scene `SceneGlobals::gameMode`).

---

## 6. WinMain Wiring

**File:** `src/app/WinMain.cpp`

```cpp
#include <game/FpsGame.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    engine::game::FpsGame game;

    engine::app::ApplicationDesc desc;
    desc.windowTitle    = L"FPS Game";
    desc.windowWidth    = 1920;
    desc.windowHeight   = 1080;
    desc.vsync          = true;
    desc.game           = &game;
    desc.startScenePath = "scenes/main.scene";

    // ... CLI parsing (--host / --connect) unchanged ...

    engine::app::Application app;
    if (!app.init(desc)) return 1;
    app.run();
    app.shutdown();
    return 0;
}
```

---

## 7. Start Scene

The start scene (`scenes/main.scene`) must exist for the release build to boot. It should contain:
- At least one camera entity.
- At least two `SpawnPointComponent` entities.
- Level geometry with collision.
- A `SceneGlobals` with `gameMode = "DeathMatch"`, `maxPlayers = 8`, `matchTimeLimit = 600.0`.

Creating this scene is an art/design task, not a code task. The engineering deliverable is that the code boots without crashing if the scene file is valid. If `scenes/main.scene` is missing, `Application::init()` logs an error and exits gracefully (already implemented).

---

## 8. Files to Create

```
src/game/CMakeLists.txt
src/game/FpsGame.h / .cpp
src/game/DeathMatchMode.h / .cpp
```

### Files to Modify

| File | Change |
|------|--------|
| `src/app/WinMain.cpp` | Add `#include <game/FpsGame.h>`; set `desc.game`, `desc.startScenePath` |
| root `CMakeLists.txt` | `add_subdirectory(src/game)` |
| `src/app/CMakeLists.txt` | Link `engine_game` to `engine` executable |

---

## 9. Tests

**File:** `tests/game/FpsGameBootstrapTests.cpp` (label: unit)

- `FpsGame::onInit()` with a headless `GameContext` (no GPU): verify no crash, archetypes registered.
- `DeathMatchMode::evaluateWinCondition()`: set score to kill limit; verify returns true.
- `DeathMatchMode::selectSpawnPoint()` with 3 points: verify returns the highest-priority one.
- `DeathMatchMode::selectSpawnPoint()` with empty list: verify returns `kInvalidEntity`.

---

## 10. Open Questions

- **Q:** Should `WinMain.cpp` remain in `src/app/` or move to `src/game/`? Recommendation: move to `src/game/` — it is the entry point for the game binary, not the engine library. The engine library stays in `src/app/`.
- **Q:** Should `DeathMatchMode` serialise its round state via `serializeState()` / `deserializeState()`? Recommendation: yes — implement both for Phase 9 so the server checkpoint system (#57) can save mid-round state correctly.
