# 03 — Scenes: Creation, Serialization, and Runtime Loading

A scene is the container for a playable map: entity pool, physics simulation, static geometry BVH, and global settings. Scenes live on disk as `.scene` files and are managed at runtime by `SceneManager`.

## Scene file format

`.scene` files use a hybrid format: a 512-byte TOML header followed by packed binary data. The magic bytes `ENGS` appear at the start of the binary section.

```
┌────────────────────────────────────────┐
│ TOML header (512 bytes, zero-padded)   │
│  magic = "ENGS"                        │
│  version = 1                           │
│  section offsets                       │
├────────────────────────────────────────┤
│ Entity Table   (entity IDs + archetype) │
│ Component SoA  (per-component arrays)  │
│ Asset Ref Table (SHA-256 refs)         │
│ SceneGlobals                           │
│ Hierarchy      (parent links)          │
└────────────────────────────────────────┘
```

The format is forward-compatible: the loader skips unknown component sections using the length field in the section header.

## `SceneGlobals`

`SceneGlobals` stores the map-wide settings accessible at runtime:

```cpp
// src/core/public/core/scene/SceneGlobals.h
namespace engine::core::scene {

struct SceneGlobals {
    std::string sceneName;
    uint32_t    sceneId      = 0;
    float       gravity      = -9.81f;     // m/s^2

    // Lighting
    engine::core::math::Vec3 ambientColor  = { 0.1f, 0.1f, 0.1f };

    // Fog
    float fogStart   = 50.f;
    float fogEnd     = 300.f;
    engine::core::math::Vec3 fogColor = { 0.7f, 0.8f, 0.9f };

    // Match settings
    float    matchTimeLimit = 600.f;     // seconds (0 = no limit)
    uint8_t  maxPlayers     = 10;
    std::string gameMode;                // e.g. "TDM", "Domination"

    // Navigation
    std::string navmeshAsset;            // relative path to .navmesh asset

    // Spawn points (world-space Transforms)
    std::vector<engine::core::math::Transform> spawnPoints;
};

} // namespace engine::core::scene
```

## `SceneManager` API

```cpp
// src/core/public/core/scene/SceneManager.h
namespace engine::core::scene {

class SceneManager {
public:
    // Load a scene from disk. Allocates the entity pool and asset handles.
    // Does NOT activate physics or systems yet — call activate() for that.
    Scene* load(std::string_view path);

    // Make a loaded scene the active simulation target.
    // Builds the static BVH, activates the PhysicsWorld, starts ticking.
    void activate(Scene* scene);

    // Deactivate without unloading (e.g. for seamless transition).
    void deactivate(Scene* scene);

    // Release all resources. Must be deactivated first.
    void unload(Scene* scene);

    // Accessors
    Scene*       getActive() const;
    Scene*       get(uint32_t sceneId) const;
    Scene*       getByName(std::string_view name) const;
    std::vector<Scene*> getAllActive() const;
};

} // namespace engine::core::scene
```

A typical single-scene load:

```cpp
void ArenaGame::onInit(engine::app::GameContext& ctx) {
    engine::core::scene::Scene* scene =
        ctx.sceneManager.load("content/maps/arena_01.scene");
    ctx.sceneManager.activate(scene);
}
```

The `ApplicationDesc::startScene` path is loaded and activated automatically before `IGame::onInit` is called, so you only need to call this manually for subsequent map changes.

## Scene lifecycle

```
load()       — deserialize .scene file → entity pool, assets in CPU memory
activate()   — build static BVH, start PhysicsWorld, enable systems
tick(dt)     — runs each game tick (called internally by GameLoop)
deactivate() — stop physics, disable systems, freeze entity pool
unload()     — free all resources
```

## Map transitions

To transition between maps without a visible hitch:

```cpp
void ArenaGame::beginMapTransition(engine::app::GameContext& ctx,
                                   std::string_view nextMapPath)
{
    // Load next scene in the background while current scene is still ticking.
    engine::core::scene::Scene* next =
        ctx.sceneManager.load(nextMapPath);

    // At the transition point (round end, loading screen shown):
    ctx.sceneManager.deactivate(ctx.sceneManager.getActive());
    ctx.sceneManager.unload(ctx.sceneManager.getActive());
    ctx.sceneManager.activate(next);
}
```

## Scene serialization API

`SceneSerializer` (in the `tools` module) handles read/write. It is used by both the editor and the server checkpoint system.

```cpp
// src/tools/public/tools/SceneSerializer.h
namespace engine::tools {

class SceneSerializer {
public:
    // Save the current ECS World state to path.
    static bool save(std::string_view path,
                     const engine::core::ecs::World& world,
                     const engine::core::scene::SceneGlobals& globals);

    // Load synchronously. Returns false if the file is missing or corrupt.
    static bool load(std::string_view path,
                     engine::core::ecs::World& world,
                     engine::core::scene::SceneGlobals& globals);

    // Async variant — calls callback on completion (game thread).
    static void loadAsync(std::string_view path,
                          engine::core::ecs::World& world,
                          engine::core::scene::SceneGlobals& globals,
                          std::function<void(bool ok)> callback);

    // Validate without loading — checks magic, version, SHA-256 asset refs.
    static bool validate(std::string_view path);
};

} // namespace engine::tools
```

## Creating a scene in the editor

See [10 — Editor & PIE](10_editor_pie.md) for the full editor workflow. The short version:

1. **File → New Scene** (Ctrl+N) — creates an empty scene with default `SceneGlobals`.
2. Drag `.glb`/`.gltf` or `.easset` meshes from the Asset Browser into the viewport to place props.
3. Place spawn-point entities via **Add → SpawnPoint** and position them with the gizmo.
4. Open **Scene Settings** (F4) to edit `SceneGlobals` (gravity, ambient, max players, etc.).
5. **File → Save Scene** (Ctrl+S) — writes the `.scene` binary to disk.

## Loading a scene at runtime (client)

On the client, scene loading is triggered by the server's level-change message. The engine handles this internally; `IGame::onInit` is called again on the new scene's context. If you need to reinitialize game-mode state after a map change:

```cpp
void ArenaGame::onInit(engine::app::GameContext& ctx) {
    // This is called on every map load — reset anything map-specific here.
    gameMode_.onRoundStart(ctx, 1);
    engine::core::fps::registerFpsArchetypes(ctx.entityFactory);
}
```

## Next

[04 — Entities](04_entities.md): `EntityFactory`, FPS archetypes, and ECS components.
