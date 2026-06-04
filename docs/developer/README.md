# Developer Guide — Arena FPS on the Engine

This guide walks through building an online arena FPS using the engine's full stack. Each document covers one layer of the system, from bootstrapping the application down to persistent player data. The example used throughout is a 10-player team deathmatch game.

## Documents

| # | Topic | What it covers |
|---|-------|----------------|
| [01](01_bootstrap.md) | Bootstrap | `IGame`, `Application`, `ApplicationDesc` |
| [02](02_game_mode.md) | Game Mode | `IGameMode`, round lifecycle, win conditions |
| [03](03_scenes.md) | Scenes | Editor workflow, `.scene` format, runtime loading |
| [04](04_entities.md) | Entities | `EntityFactory`, FPS archetypes, ECS components |
| [05](05_input.md) | Input | `InputSystem`, `input.toml`, `InputFrame` |
| [06](06_physics_movement.md) | Physics & Movement | `PhysicsWorld`, `CharacterController`, tick integration |
| [07](07_combat.md) | Combat | `DamageSystem`, `HitscanValidationRequest`, `EventBus` |
| [08](08_networking.md) | Networking | Replication, prediction, interpolation, RPCs |
| [09](09_persistence.md) | Persistence | `SaveSystem`, `PlayerProfile`, `MatchRecord`, checkpoints |
| [10](10_editor_pie.md) | Editor & PIE | `EngineEditor`, panels, Play-in-Editor |

## Prerequisites

- Engine built with the `devrel` preset for editor access; `debug` for development iteration.
- vcpkg dependencies installed (see root `vcpkg.json`).
- A DX12-capable GPU for rendering; unit tests run headless.

## Conventions used in code samples

All engine types live under `engine::` sub-namespaces. For brevity the samples below use:

```cpp
using namespace engine::app;
using namespace engine::core;
using namespace engine::core::ecs;
using namespace engine::core::math;
```

These `using` declarations are fine inside `.cpp` files. Never place them in headers.
