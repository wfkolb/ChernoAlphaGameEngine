# Scene System: Building and Managing Scenes

**Phase 6 — Physics & Scene Lead Reference**
**Engine:** Windows DX12 / C++20 | `engine::core` module

---

## Table of Contents

1. [What Is a Scene?](#1-what-is-a-scene)
2. [Scene Lifecycle](#2-scene-lifecycle)
3. [Creating a Scene](#3-creating-a-scene)
4. [Static vs Dynamic Entities](#4-static-vs-dynamic-entities)
5. [Level Geometry](#5-level-geometry)
6. [Spatial Partitioning](#6-spatial-partitioning)
7. [Scene-Global Data](#7-scene-global-data)
8. [Trigger Volumes](#8-trigger-volumes)
9. [Player Spawn Points](#9-player-spawn-points)
10. [Scene Streaming (Stub)](#10-scene-streaming-stub)
11. [Managing Multiple Scenes](#11-managing-multiple-scenes)
12. [SceneManager API](#12-scenemanager-api)

---

## 1. What Is a Scene?

A **Scene** is the runtime container for a single game level (or logical context, such as a main menu). It owns:

- The **entity pool** — all `EntityId` slots and their generation counters.
- **Archetype storage** — per-archetype chunked SoA arrays of component data.
- **Spatial structures** — the static BVH, the dynamic broad-phase grid, and the trigger volume list.
- **Physics world** — one `PhysicsWorld` instance running at 64 Hz.
- **Scene-global data** — gravity, spawn points, ambient settings, scene name/ID.
- **The asset manifest** — resolved handles to every mesh, material, and physics material referenced by entities in this scene.

A scene corresponds one-to-one with a `.scene` file on disk. At runtime, one or more scenes can be active simultaneously (e.g. additive loading of a game scene on top of a persistent HUD scene).

```cpp
namespace engine::core {

class Scene {
public:
    // Entity management
    EntityId        createEntity(std::string_view archetypeName);
    void            destroy(EntityId id);
    bool            isValid(EntityId id) const;

    // Component access
    template<typename C> C&        get(EntityId id);
    template<typename C> C*        try_get(EntityId id);
    template<typename C> bool      has(EntityId id) const;
    template<typename C> C&        add(EntityId id);
    template<typename C> void      remove(EntityId id);

    // Hierarchy
    void            setParent(EntityId child, EntityId parent);
    void            clearParent(EntityId child);
    std::span<const EntityId> getChildren(EntityId parent) const;

    // Queries
    template<typename... Cs, typename Fn> void query(Fn&&);
    template<typename ArchetypeTag>       auto view();

    // Spatial queries (delegated to PhysicsWorld)
    bool            raycast(const Ray& ray, RaycastHit& hit, const QueryFilter& filter = {}) const;
    int             overlapSphere(const Vec3& center, float radius,
                                  std::span<RaycastHit> results,
                                  const QueryFilter& filter = {}) const;

    // Scene globals
    SceneGlobals&   globals();
    PhysicsWorld&   physicsWorld();
    EntityFactory&  getEntityFactory();
};

} // namespace engine::core
```

---

## 2. Scene Lifecycle

```
Load ──► Activate ──► [Tick Loop] ──► Deactivate ──► Unload
  │           │              │               │             │
parse      register       systems        stop sys     free all
.scene     systems        run each       flush events  memory
file       start BVH      fixed tick     drain queues
           build
```

### Load

`SceneManager::load(path)` is asynchronous by default. It:

1. Parses the `.scene` file header.
2. Allocates archetype storage.
3. Deserialises component arrays.
4. Issues async asset loads (meshes, textures, physics materials) via `AssetManager`.
5. Builds the static BVH once all static entity transforms are resolved.
6. Hands the scene to the `PhysicsWorld` for collider initialisation.
7. Sets the scene state to `Ready` and calls all registered `SceneLoadListener` callbacks.

### Activate

`SceneManager::activate(sceneId)` makes a scene the target of system updates and rendering. A scene can be loaded but not active (preloaded next level). Multiple scenes can be active simultaneously.

### Tick

Each engine tick (64 Hz fixed):

1. `InputSystem` collects input events.
2. Game systems run (move characters, fire weapons, etc.).
3. `PhysicsWorld::step(fixedDt)` advances physics.
4. `TransformSystem` propagates hierarchies.
5. Rendering submits draw calls for all active scenes (merged into one frame).

### Deactivate / Unload

`deactivate` stops systems from processing the scene's entities without freeing memory. `unload` frees all entity and component memory and releases asset handles.

---

## 3. Creating a Scene

### From the Editor

The editor presents a blank scene with a default camera and a directional light. The developer:

1. Places static mesh entities to form the level.
2. Configures physics materials, colliders, trigger volumes.
3. Places spawn points, pickup entities, etc.
4. Sets scene globals (gravity, ambient light intensity, navmesh reference).
5. Saves to a `.scene` file.

The editor writes the `.scene` binary format directly (see the Serialization doc for the file layout).

### Programmatically (Runtime or Test)

```cpp
Scene* scene = SceneManager::get().createEmpty("test_arena");

// Add scene globals
scene->globals().gravity          = Vec3{0.f, -9.81f, 0.f};
scene->globals().sceneName        = "test_arena";
scene->globals().ambientIntensity = 0.12f;

// Build the floor
EntityId floor = scene->createEntity("StaticProp");
scene->get<Transform>(floor).position = Vec3{0.f, -0.5f, 0.f};
scene->get<Transform>(floor).scale    = Vec3{50.f, 1.f, 50.f};
scene->get<Collider>(floor).shape     = ColliderShape::box(Vec3{50.f, 1.f, 50.f});
scene->get<Collider>(floor).layer     = PhysicsLayer::StaticWorld;

// Finalize static geometry into BVH
scene->bakeBVH();

SceneManager::get().activate(scene);
```

### Loading from a .scene File

```cpp
SceneHandle handle = SceneManager::get().load("levels/arena_01.scene",
    SceneLoadOptions{
        .async          = true,
        .additive       = false,
        .onLoaded = [](Scene* scene) {
            LOG_INFO("Scene loaded: {} entities", scene->entityCount());
        }
    }
);
```

---

## 4. Static vs Dynamic Entities

| Property | Static | Dynamic |
|---|---|---|
| Transform changes after bake? | No | Yes |
| Included in BVH? | Yes (baked) | No |
| Included in broad-phase grid? | No | Yes |
| Has `RigidBody`? | No | Yes (dynamic/kinematic) |
| Memory layout | Sorted by spatial position at bake | Updated per tick |
| Rendering batching | Instanced draw batch | Per-entity draw call |

An entity is considered **static** if:

- It has a `Collider` but no `RigidBody` component, **or**
- Its `RigidBody.flags` includes `RB_FLAG_STATIC`.

Call `Scene::bakeBVH()` after placing all static entities. This call is idempotent but expensive; call it once at load time (the editor calls it automatically on save).

Marking a static entity as dirty after baking is a logged error in debug builds. Correct workflows never move static geometry at runtime.

---

## 5. Level Geometry

### The StaticMesh + TriMeshCollider Pattern

For large, irregular level geometry (the world mesh — walls, ceilings, floors, corridors), use a `TriangleMesh` collider baked from the visual mesh.

```cpp
EntityId worldMesh = scene->createEntity("StaticProp");
auto& mesh     = scene->get<Mesh>(worldMesh);
auto& material = scene->get<Material>(worldMesh);
auto& collider = scene->get<Collider>(worldMesh);

mesh.assetHandle     = AssetManager::get().load<MeshAsset>("meshes/arena_world.mesh");
material.assetHandle = AssetManager::get().load<MaterialAsset>("materials/concrete.mat");

collider.shape = ColliderShape::triangleMesh(
    AssetManager::get().load<PhysicsMeshAsset>("meshes/arena_world.phys")
);
collider.layer = PhysicsLayer::StaticWorld;
// No RigidBody component — this is purely static
```

The `.phys` file is a pre-cooked triangle soup generated by the editor from the visual mesh. It is a separate asset to allow simplified collision geometry to diverge from the visual LOD.

### Box/Convex Primitives for Smaller Props

For crates, barriers, and other convex props, prefer `ColliderShape::box` or `ColliderShape::convexHull`. Triangle mesh colliders are expensive in narrow phase; reserve them for the world mesh.

```cpp
// A crate
EntityId crate = scene->createEntity("StaticProp");
scene->get<Transform>(crate).position = Vec3{5.f, 0.5f, 3.f};
scene->get<Collider>(crate).shape     = ColliderShape::box(Vec3{0.5f, 0.5f, 0.5f});
scene->get<Collider>(crate).layer     = PhysicsLayer::Default;
```

### World Mesh Guidelines

- Keep the world mesh as a **single entity** per scene cell (or one per logical region for streaming).
- The `.phys` asset should be no more than 200k triangles for acceptable narrow-phase performance.
- Author collision geometry at roughly 1/4 the visual poly count — players will not notice.
- Avoid T-junctions and degenerate triangles in the physics mesh; the cooker will warn about these.

---

## 6. Spatial Partitioning

### Static BVH (Bounding Volume Hierarchy)

After `bakeBVH()` is called, all static entities (those without a `RigidBody`) are inserted into an AABB-based BVH. The tree is built once and never mutated at runtime.

- **Leaf node:** references a single collider (a `Collider` component on a static entity).
- **Internal node:** AABB enclosing all leaves in its subtree.
- **Traversal:** ray/sphere/box queries descend the tree skipping nodes whose AABB does not intersect the query volume.

The BVH is stored inside `PhysicsWorld` and is queried transparently via `Scene::raycast()` and `Scene::overlapSphere()`.

### Dynamic Broad-Phase Grid

Dynamic entities (those with a `RigidBody`) are tracked in a uniform spatial grid. Each cell is 4m × 4m. The grid is updated every physics tick as entities move. Cells store lists of `EntityId`s.

Grid queries return candidate pairs for narrow-phase testing. The grid is not queried directly by game code — use the `PhysicsWorld` query API.

### Querying Spatial Structures from Game Code

```cpp
// Raycast from camera for weapon hit detection
Ray ray = { cameraPos, cameraForward };
RaycastHit hit;
QueryFilter filter;
filter.layerMask  = PhysicsLayer::StaticWorld | PhysicsLayer::Player | PhysicsLayer::Dynamic;
filter.excludeEntity = playerEntityId;   // don't hit yourself

if (scene->raycast(ray, hit, filter)) {
    LOG_INFO("Hit entity {} at distance {:.2f}m", hit.entityId, hit.distance);
    // Apply damage, spawn decal, etc.
}

// Sphere overlap for explosion radius damage
std::array<RaycastHit, 32> overlaps;
int count = scene->overlapSphere(explosionPos, blastRadius, overlaps, filter);
for (int i = 0; i < count; ++i) {
    apply_explosive_damage(overlaps[i].entityId, explosionPos, scene);
}
```

```cpp
struct RaycastHit {
    Vec3        point;          // world-space impact point
    Vec3        normal;         // surface normal at impact
    float       distance;       // metres along ray
    EntityId    entityId;       // entity that was hit
    uint32_t    physMatIndex;   // index into physics_materials.toml
};
```

---

## 7. Scene-Global Data

All scene-wide settings live in `SceneGlobals`, accessible via `Scene::globals()`.

```cpp
struct SceneGlobals {
    // Physics
    Vec3        gravity             = Vec3{0.f, -9.81f, 0.f};

    // Rendering (placeholder — expanded in Phase 7)
    float       ambientIntensity    = 0.1f;
    Vec3        ambientColor        = Vec3{1.f, 1.f, 1.f};
    bool        fogEnabled          = false;
    float       fogDensity          = 0.01f;

    // Identity
    std::string sceneName;
    uint32_t    sceneId             = 0;   // stable hash of sceneName

    // Gameplay
    float       matchTimeLimit      = 600.f;  // seconds; 0 = unlimited
    uint8_t     maxPlayers          = 16;
    std::string gameMode;                     // "tdm", "ctf", "dm", etc.

    // Navmesh
    AssetHandle navmeshAsset;                 // null if no navmesh in this scene

    // Spawn points (filled during BVH bake from SpawnPointEntity instances)
    std::vector<SpawnPointRef> spawnPoints;
};

struct SpawnPointRef {
    EntityId entityId;
    Vec3     position;
    Quat     rotation;
    uint8_t  teamId;
};
```

---

## 8. Trigger Volumes

A trigger volume is a `Collider` with `isTrigger = true`. It generates overlap events but produces no collision response forces. Implemented as a `TriggerEntity` archetype (see the Entity System doc).

### Setting Up a Trigger

```cpp
EntityId door_trigger = scene->createEntity("Trigger");
auto& transform  = scene->get<Transform>(door_trigger);
auto& collider   = scene->get<Collider>(door_trigger);
auto& callback   = scene->get<TriggerCallback>(door_trigger);

transform.position   = Vec3{20.f, 1.f, 0.f};
collider.shape       = ColliderShape::box(Vec3{2.f, 2.f, 0.3f});
collider.isTrigger   = true;
collider.layer       = PhysicsLayer::Trigger;
// Trigger layer overlaps with Player layer (set in layer collision matrix)

callback.onEnter = [door_entity_id](EntityId enterer, Scene& scene) {
    if (scene.has<CharacterController>(enterer)) {
        scene.get<DoorState>(door_entity_id).open = true;
    }
};
callback.onExit = [door_entity_id](EntityId leaver, Scene& scene) {
    scene.get<DoorState>(door_entity_id).open = false;
};
```

### Trigger Event Flow

Every physics tick, the `TriggerSystem`:

1. Queries the `PhysicsWorld` for all (trigger, dynamic-entity) overlap pairs.
2. Compares with the previous tick's set to classify as **Enter**, **Stay**, or **Exit**.
3. Invokes the corresponding `TriggerCallback` function.

`onEnter` and `onExit` are guaranteed to fire exactly once per transition. `onStay` fires every tick while overlap persists.

### Built-In Trigger Archetypes

| Trigger Purpose | Recommended Setup |
|---|---|
| Zone boundary / level exit | `onEnter` → `SceneManager::transition()` |
| Objective zone (CTF flag) | `onEnter`/`onExit` update objective state |
| Damage zone (lava, fall zone) | `onStay` → apply periodic damage |
| Pickup detection | `onEnter` → award pickup, destroy pickup entity |

---

## 9. Player Spawn Points

### Placement

Spawn points are `SpawnPointEntity` instances. Place them in the editor at player-height above the floor so the spawned capsule does not overlap geometry.

```
SpawnPointEntity fields (in editor):
  Position:    floor_pos + Vec3{0, 1.0f, 0}   (capsule center at eye height from floor)
  Rotation:    facing direction for spawned player
  TeamId:      0 = any team, 1 = team A, 2 = team B
```

### Spawn Selection Strategy

The `SpawnSelectionSystem` is called by the server when placing a newly connected or respawning player. It runs in `engine::core` but the strategy is replaceable by game code.

**Default strategy — Furthest From Enemies:**

```cpp
EntityId select_spawn_point(Scene& scene, uint8_t teamId) {
    auto& globals = scene.globals();
    EntityId best = NULL_ENTITY;
    float    bestDist = -1.f;

    for (auto& sp : globals.spawnPoints) {
        if (sp.teamId != 0 && sp.teamId != teamId) continue;

        float minEnemyDist = FLT_MAX;
        scene.query<const Transform, const TeamTag>(
            [&](EntityId id, const Transform& t, const TeamTag& tag) {
                if (tag.teamId == teamId) return;  // skip allies
                float d = Vec3::distance(t.position, sp.position);
                minEnemyDist = std::min(minEnemyDist, d);
            }
        );

        if (minEnemyDist > bestDist) {
            bestDist = minEnemyDist;
            best     = sp.entityId;
        }
    }
    return best;
}
```

**Alternate strategies** can be registered:

```cpp
SpawnSelectionSystem::setStrategy(SpawnStrategy::RoundRobin);
SpawnSelectionSystem::setStrategy(SpawnStrategy::FurthestFromEnemies);  // default
SpawnSelectionSystem::setCustomStrategy(my_strategy_fn);
```

---

## 10. Scene Streaming (Stub)

Full streaming is a Phase 8 deliverable. This section documents the intended architecture so that scenes authored today are forward-compatible.

### Zones and Cells

A large map is divided into rectangular **cells** (typically 64m × 64m). Each cell is a separate `.scene` file on disk. A **zone manifest** file (`zone.manifest.toml`) lists all cells and their world-space bounding boxes.

```toml
# zone.manifest.toml
[zone]
name = "industrial_district"

[[cell]]
id   = "cell_00_00"
file = "levels/industrial/cell_00_00.scene"
bounds_min = [-32, -10, -32]
bounds_max = [ 32,  50,  32]

[[cell]]
id   = "cell_01_00"
file = "levels/industrial/cell_01_00.scene"
bounds_min = [ 32, -10, -32]
bounds_max = [ 96,  50,  32]
```

### Streaming Budget

- **Active radius:** cells within 96m of any player are fully loaded.
- **Preload radius:** cells within 160m are loaded but not activated.
- **Unload radius:** cells beyond 200m are deactivated and unloaded.

The `ZoneStreamingManager` (stub class — not yet implemented) will manage this lifecycle.

### Portal-Based Visibility Culling (Future)

Each cell stores a list of portals (AABBs on cell boundaries). The renderer will use portals to cull cells that are not reachable from the camera's viewing frustum. This is relevant for corridor-heavy FPS maps.

---

## 11. Managing Multiple Scenes

Multiple scenes can be loaded and active simultaneously. Common use cases:

| Use Case | Pattern |
|---|---|
| Main menu + game level | Load menu first; on "Play", load game level additively then unload menu |
| Persistent HUD entities | A tiny "hud.scene" is always active; never unloaded |
| Cinematic cutscene | Load cutscene scene additively; deactivate gameplay scene; reverse when done |
| Level streaming cells | Each cell is a separate scene loaded additively |

### Scene Isolation

By default, scenes are isolated: entities in Scene A cannot directly reference entities in Scene B by `EntityId`. Cross-scene references are resolved through:

- **Named entities:** `SceneManager::findEntityByName(globalName)` searches all active scenes.
- **Events:** scenes communicate by posting events to the global `EventBus`.

### Rendering Order

Active scenes are rendered in activation order. A scene marked `renderOrder = RenderOrder::Background` renders first (for skyboxes); `RenderOrder::Foreground` renders last (for screen-space HUD geometry).

---

## 12. SceneManager API

```cpp
namespace engine::core {

class SceneManager {
public:
    static SceneManager& get();

    // Loading
    SceneHandle  load(std::string_view path, const SceneLoadOptions& opts = {});
    void         unload(SceneHandle handle);

    // Activation
    void         activate(SceneHandle handle);
    void         deactivate(SceneHandle handle);

    // Querying
    Scene*       getActive();                      // primary active scene
    Scene*       get(SceneHandle handle);
    Scene*       getByName(std::string_view name);
    std::span<Scene*> getAllActive();

    // Transitions (cross-fade, load-screen, etc.)
    void         transition(std::string_view targetPath,
                            const TransitionOptions& opts = {});

    // Empty scene creation (for runtime/testing)
    Scene*       createEmpty(std::string_view name);
};

struct SceneLoadOptions {
    bool     async    = true;
    bool     additive = false;   // if false, unloads current active scene first
    std::function<void(Scene*)> onLoaded;
    std::function<void(std::string_view error)> onError;
};

struct TransitionOptions {
    float    fadeOutDuration  = 0.25f;  // seconds
    float    fadeInDuration   = 0.25f;
    bool     keepCurrentAudio = false;
    std::function<void()> onMidpoint;   // called when screen is fully black
};

} // namespace engine::core
```

### Typical FPS Game Flow

```cpp
// Application startup
SceneManager::get().load("menus/main_menu.scene",
    SceneLoadOptions{
        .onLoaded = [](Scene* scene) {
            SceneManager::get().activate(scene);
        }
    }
);

// When the player clicks "Play"
void on_play_clicked() {
    SceneManager::get().transition("levels/arena_01.scene",
        TransitionOptions{
            .fadeOutDuration = 0.3f,
            .fadeInDuration  = 0.5f,
            .onMidpoint      = []() {
                // Optionally preload assets while screen is black
            }
        }
    );
}
```

### SceneHandle vs Scene*

`SceneHandle` is an opaque 32-bit token stable across loads. `Scene*` is the live pointer, which is only valid while the scene is loaded. Prefer `SceneHandle` for storage; resolve to `Scene*` at call sites via `SceneManager::get(handle)`.
