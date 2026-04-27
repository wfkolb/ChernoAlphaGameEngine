# Entity System: Defining and Using Entities

**Phase 6 — Physics & Scene Lead Reference**
**Engine:** Windows DX12 / C++20 | `engine::core` module

---

## Table of Contents

1. [What Is an Entity?](#1-what-is-an-entity)
2. [Built-In Component Types](#2-built-in-component-types)
3. [Defining a Custom Component](#3-defining-a-custom-component)
4. [Archetypes: Defining Entity Types](#4-archetypes-defining-entity-types)
5. [EntityFactory: Spawning Entities](#5-entityfactory-spawning-entities)
6. [FPS-Specific Entity Archetypes](#6-fps-specific-entity-archetypes)
7. [Entity Lifecycle](#7-entity-lifecycle)
8. [Querying Entities](#8-querying-entities)
9. [Entity Parenting and Transform Hierarchy](#9-entity-parenting-and-transform-hierarchy)

---

## 1. What Is an Entity?

An entity is a lightweight, opaque **64-bit identifier** (`EntityId`). It carries no data itself — all data lives in **components** that are attached to it. The ECS is **archetype-based**: every entity belongs to exactly one archetype at a time, and all entities sharing the same set of component types are stored contiguously in the same archetype chunk.

```cpp
namespace engine::core {

// A 64-bit handle. Upper 32 bits = generation counter; lower 32 bits = index.
using EntityId = uint64_t;

constexpr EntityId NULL_ENTITY = 0;

// Decompose an EntityId into its parts.
inline uint32_t entity_index(EntityId id)      { return static_cast<uint32_t>(id); }
inline uint32_t entity_generation(EntityId id) { return static_cast<uint32_t>(id >> 32); }

} // namespace engine::core
```

The **generation counter** prevents use-after-free: when a slot is recycled, the generation increments, invalidating any stale `EntityId` references held by game code.

### Why Archetypes?

Systems iterate over large numbers of entities per tick. Keeping all components for a given archetype in contiguous arrays (Structure of Arrays, SoA within a chunk) maximizes cache utilisation. A query for "all entities with Transform + RigidBody" resolves to a small set of archetype chunks and walks them linearly.

```
Archetype[Transform | RigidBody | Collider]
  Chunk 0:  [T0,T1,...,T511] [R0,R1,...,R511] [C0,C1,...,C511]
  Chunk 1:  [T512,...,T1023] ...
```

Moving an entity to a new archetype (e.g. adding a component) copies it to the new archetype's storage and leaves a hole that is filled by swapping with the last entity.

---

## 2. Built-In Component Types

All built-in components are declared in `engine/core/components.h`. Game code includes this header and links against `engine_core`.

| Component | Header | Description |
|---|---|---|
| `Transform` | `core/components/transform.h` | World-space position (`Vec3`), rotation (`Quat`), scale (`Vec3`). Row-major, right-handed, Y-up. Also stores a cached `Mat4` local-to-world matrix. |
| `Mesh` | `core/components/mesh.h` | Handle to a `MeshAsset` (vertex/index buffers). Specifies LOD group and shadow-casting flags. |
| `Material` | `core/components/material.h` | Handle to a `MaterialAsset`. Maps to a DX12 PSO + descriptor heap allocation. |
| `Camera` | `core/components/camera.h` | Field of view (vertical, radians), near/far planes, projection mode. The renderer uses the entity flagged as the active camera. |
| `RigidBody` | `core/components/rigidbody.h` | Mass, linear/angular velocity, damping coefficients, flags (static/kinematic/dynamic). Owned by the physics system. |
| `Collider` | `core/components/collider.h` | Shape description (box, sphere, capsule, convex, trimesh), physics material reference, layer mask, trigger flag. |
| `CharacterController` | `core/components/character_controller.h` | Capsule dimensions (radius, half-height), step-up height, max slope angle, ground state. Velocity is set directly by game code each tick. |
| `NetworkIdentity` | `core/components/network_identity.h` | Stable `NetworkId` (uint32) assigned by the server; replication authority flags; dirty-bit mask for component fields. |
| `InputReceiver` | `core/components/input_receiver.h` | Marks an entity as an input consumer. Binds an `InputContext` (action map) to this entity. |
| `Name` | `core/components/name.h` | A short string label (up to 64 chars, stored inline) for editor display and debug logging. |
| `Health` | `core/components/health.h` | Current HP, max HP, armour value, dead flag, last damage source `EntityId`. |
| `Lifetime` | `core/components/lifetime.h` | Remaining lifetime in seconds. The `LifetimeSystem` decrements this each tick and destroys the entity when it reaches zero. Used for projectiles, effects. |

### Component Data Layouts (excerpts)

```cpp
struct Transform {
    Vec3  position  = Vec3::zero();
    Quat  rotation  = Quat::identity();
    Vec3  scale     = Vec3::one();
    Mat4  localToWorld;         // rebuilt each frame by TransformSystem
    EntityId parent = NULL_ENTITY;
};

struct RigidBody {
    float    mass               = 1.0f;
    float    linearDamping      = 0.05f;
    float    angularDamping     = 0.05f;
    float    friction           = 0.5f;
    float    restitution        = 0.2f;
    Vec3     linearVelocity     = Vec3::zero();
    Vec3     angularVelocity    = Vec3::zero();
    uint32_t flags              = RB_FLAG_DYNAMIC;
    // RB_FLAG_STATIC | RB_FLAG_KINEMATIC | RB_FLAG_DYNAMIC
    // RB_FLAG_DISABLE_GRAVITY | RB_FLAG_FREEZE_ROTATION
};

struct Health {
    float    current   = 100.0f;
    float    maximum   = 100.0f;
    float    armour    = 0.0f;
    bool     isDead    = false;
    EntityId lastDamager = NULL_ENTITY;
};

struct Lifetime {
    float remaining = 5.0f;   // seconds
};
```

---

## 3. Defining a Custom Component

A component is **any plain C++ struct** (trivially copyable preferred; non-trivial types are supported with a registered destructor). Register it with `ComponentRegistry` before any entity of that type is spawned.

### Step 1 — Declare the struct

```cpp
// game/components/weapon_state.h
#pragma once
#include <engine/core/entity_id.h>

namespace game {

enum class WeaponFireMode : uint8_t { Semi, Burst, Auto };

struct WeaponState {
    float           ammoReserve      = 90.0f;
    float           ammoInMag        = 30.0f;
    float           magCapacity      = 30.0f;
    float           cooldownRemaining = 0.0f;   // seconds until next shot
    float           reloadTimeLeft   = 0.0f;
    WeaponFireMode  fireMode         = WeaponFireMode::Semi;
    bool            isReloading      = false;
    engine::core::EntityId ownerEntity = engine::core::NULL_ENTITY;
};

} // namespace game
```

### Step 2 — Register with ComponentRegistry

```cpp
// game/game_init.cpp
#include <engine/core/component_registry.h>
#include "game/components/weapon_state.h"

void register_game_components() {
    using namespace engine::core;

    ComponentRegistry::get().register_component<game::WeaponState>(
        "WeaponState",
        ComponentFlags::Replicated   // include in network snapshots
    );
}
```

Registration must happen before `SceneManager::load()` or `EntityFactory::spawn()` is first called. Call it in your game's startup sequence, before the first tick.

### Step 3 — Serialization hooks (optional but recommended)

```cpp
ComponentRegistry::get().register_serializer<game::WeaponState>(
    // Serialize
    [](const game::WeaponState& ws, ByteWriter& w) {
        w.write(ws.ammoReserve);
        w.write(ws.ammoInMag);
        w.write(ws.magCapacity);
        w.write(static_cast<uint8_t>(ws.fireMode));
    },
    // Deserialize
    [](game::WeaponState& ws, ByteReader& r) {
        ws.ammoReserve  = r.read<float>();
        ws.ammoInMag    = r.read<float>();
        ws.magCapacity  = r.read<float>();
        ws.fireMode     = static_cast<game::WeaponFireMode>(r.read<uint8_t>());
    }
);
```

Trivially-copyable components without asset references do not need explicit serializers — the engine will `memcpy` them by default.

---

## 4. Archetypes: Defining Entity Types

An **archetype** is a named, compile-time-stable set of component types. It is the template from which entities are stamped out. Archetypes are defined in code or in `.archetype` data files loaded at startup.

### Declaring an Archetype in Code

```cpp
// game/archetypes/projectile_archetype.h
#pragma once
#include <engine/core/archetype.h>
#include <engine/core/components.h>
#include "game/components/weapon_state.h"

namespace game {

using ProjectileArchetype = engine::core::Archetype<
    engine::core::Transform,
    engine::core::RigidBody,
    engine::core::Collider,
    engine::core::Lifetime,
    engine::core::NetworkIdentity
>;

} // namespace game
```

### Registering an Archetype

```cpp
void register_archetypes() {
    using namespace engine::core;

    ArchetypeRegistry::get().add<game::ProjectileArchetype>("Projectile");
    ArchetypeRegistry::get().add<game::PlayerArchetype>("Player");
    ArchetypeRegistry::get().add<game::WeaponArchetype>("Weapon");
    // ... etc.
}
```

### Runtime Archetype Mutation

Adding or removing a component at runtime moves the entity to a new archetype. This is a relatively expensive operation (copy + swap) and should not happen every tick.

```cpp
// Attaches a FlashEffect component, migrating the entity to a new archetype
world.add_component<FlashEffect>(entityId, FlashEffect{ .duration = 0.1f });

// Removes the component, migrating back
world.remove_component<FlashEffect>(entityId);
```

---

## 5. EntityFactory: Spawning Entities

`EntityFactory` is the primary way game code creates new entities. It stamps an entity from a registered archetype, optionally applies an initialiser, and returns the new `EntityId`.

```cpp
namespace engine::core {

class EntityFactory {
public:
    // Spawn by string name (runtime archetype lookup)
    EntityId spawn(std::string_view archetypeName, const SpawnParams& params = {});

    // Spawn by compile-time archetype tag (preferred — zero-cost lookup)
    template<typename ArchetypeTag>
    EntityId spawn(const SpawnParams& params = {});

    // Spawn with an initialiser callback that receives the new entity's components
    template<typename ArchetypeTag, typename InitFn>
    EntityId spawn(InitFn&& init, const SpawnParams& params = {});
};

struct SpawnParams {
    Vec3     position  = Vec3::zero();
    Quat     rotation  = Quat::identity();
    EntityId parent    = NULL_ENTITY;
    Scene*   scene     = nullptr;    // null = active scene
};

} // namespace engine::core
```

### Usage Examples

```cpp
EntityFactory& factory = scene.getEntityFactory();

// Spawn a projectile at a muzzle position
EntityId bullet = factory.spawn<game::ProjectileArchetype>(
    [](auto& transform, auto& rigidBody, auto& collider, auto& lifetime, auto& netId) {
        transform.position  = muzzleWorldPos;
        transform.rotation  = muzzleRotation;
        rigidBody.linearVelocity = forwardDir * 800.0f;  // 800 m/s
        collider.shape      = ColliderShape::sphere(0.01f);
        collider.layer      = PhysicsLayer::Projectile;
        lifetime.remaining  = 3.0f;
        netId.authority     = NetworkAuthority::Server;
    },
    SpawnParams{ .position = muzzleWorldPos }
);

// Spawn a static prop by name (e.g. loaded from a scene file)
EntityId crate = factory.spawn("StaticProp",
    SpawnParams{ .position = Vec3{10.f, 0.f, 5.f} }
);
```

---

## 6. FPS-Specific Entity Archetypes

These archetypes are provided by the engine's FPS starter layer (`engine/fps/archetypes.h`) and can be used as-is or extended.

### PlayerEntity

The player character. One instance per connected client on the server; a local copy on the owning client.

**Components:** `Transform`, `CharacterController`, `Camera`, `InputReceiver`, `Health`, `NetworkIdentity`, `Name`

Optional game-layer additions: `WeaponSlot`, `PlayerStats`, `TeamTag`

```cpp
// Default initialisation
PlayerEntity player;
player.characterController.capsuleRadius    = 0.35f;
player.characterController.capsuleHalfHeight = 0.85f;
player.characterController.stepUpHeight     = 0.25f;
player.characterController.maxSlopeAngle    = 46.0f;  // degrees
player.health.maximum = 100.0f;
player.health.current = 100.0f;
player.camera.fovY    = glm_radians(90.0f);
player.camera.nearZ   = 0.05f;
player.camera.farZ    = 1000.0f;
```

### WeaponEntity

A weapon held or dropped in the world. Parented to a player hand socket when held.

**Components:** `Transform`, `Mesh`, `Material`, `Collider`, `NetworkIdentity`, `WeaponState`

- `Collider.isTrigger = true` when holstered (pickup detection only).
- Parent entity set to the player hand socket entity when equipped.

### ProjectileEntity

A high-velocity physics object. Destroyed by `LifetimeSystem` or on first collision.

**Components:** `Transform`, `RigidBody`, `Collider`, `Lifetime`, `NetworkIdentity`

- `RigidBody.flags = RB_FLAG_DYNAMIC | RB_FLAG_DISABLE_GRAVITY` for hitscan tracers.
- `Collider.layer = PhysicsLayer::Projectile` — collides with `StaticWorld` and `Player`, not with other projectiles.

### StaticPropEntity

Level furniture (crates, barrels, structural elements). Baked into the BVH at load time.

**Components:** `Transform`, `Mesh`, `Material`, `Collider`

- `Collider.shape` is typically `ColliderShape::box(...)` or `ColliderShape::convexHull(assetHandle)`.
- No `RigidBody` component — static props are immovable.

### TriggerEntity

An invisible volume that fires callbacks when an entity enters, stays, or exits.

**Components:** `Transform`, `Collider` (trigger mode), `TriggerCallback`

```cpp
struct TriggerCallback {
    std::function<void(EntityId enterer, Scene& scene)> onEnter;
    std::function<void(EntityId stayer,  Scene& scene)> onStay;
    std::function<void(EntityId leaver,  Scene& scene)> onExit;
};
```

### SpawnPointEntity

Marks a location where players can be spawned.

**Components:** `Transform`, `TeamTag`

```cpp
struct TeamTag {
    uint8_t teamId = 0;    // 0 = neutral / any team
};
```

The spawn selection system reads all `SpawnPointEntity` instances from the scene and selects the best candidate (furthest from living enemies by default — see Scene System doc).

### PickupEntity

An item that a player can walk over to collect (health packs, ammo, weapons).

**Components:** `Transform`, `Mesh`, `Material`, `Collider` (trigger), `PickupData`, `Lifetime` (optional respawn timer)

```cpp
struct PickupData {
    enum class Type : uint8_t { Health, Armour, Ammo, Weapon };
    Type    type       = Type::Health;
    float   quantity   = 25.0f;
    bool    respawns   = true;
    float   respawnDelay = 30.0f;  // seconds
};
```

---

## 7. Entity Lifecycle

```
Spawn ──► Init ──► [Tick loop] ──► Destroy
              │         │               │
         OnSpawn()  per-system      OnDestroy()
                    updates
```

### Spawn

`EntityFactory::spawn()` allocates a slot, assigns a generation, places the entity in the correct archetype chunk, and sets default component values. The entity is not yet visible to systems during the current tick — it enters the active set at the start of the next tick.

### Init / OnSpawn

After construction but before the first tick, the engine calls `OnSpawn` on all registered `EntityEventListener` implementations that opted into the entity's archetype.

```cpp
class PlayerSpawnListener : public engine::core::EntityEventListener {
public:
    void onSpawn(EntityId id, Scene& scene) override {
        auto& health = scene.get<Health>(id);
        auto& netId  = scene.get<NetworkIdentity>(id);
        health.current = health.maximum;
        LOG_INFO("Player {} spawned with {} HP", netId.networkId, health.current);
    }
    void onDestroy(EntityId id, Scene& scene) override {
        LOG_INFO("Player {} destroyed", id);
    }
    ArchetypeMask listenMask() const override {
        return ArchetypeMask::of<PlayerArchetype>();
    }
};
```

Register listeners during game startup:

```cpp
scene.addEntityEventListener(std::make_unique<PlayerSpawnListener>());
```

### Tick (Per-System Updates)

During each fixed tick the engine runs systems in a defined order. Each system declares which components it reads and writes; the scheduler can parallelise non-conflicting systems.

```
[InputSystem]          reads:  InputReceiver                   writes: InputReceiver.state
[CharacterMoveSystem]  reads:  InputReceiver, CharacterController  writes: CharacterController.velocity
[PhysicsWorld::step]   reads/writes: RigidBody, Transform, Collider
[LifetimeSystem]       reads/writes: Lifetime                   destroys entity at <= 0
[HealthSystem]         reads/writes: Health                     fires DeathEvent
[NetworkSystem]        reads:  NetworkIdentity, (all replicated components)
```

### Destroy

Call `scene.destroy(entityId)` from game code. The entity is marked for deferred destruction and is removed at the end of the current tick. `OnDestroy` fires before removal from archetype storage.

```cpp
// From a collision callback:
void on_projectile_hit(EntityId projectileId, EntityId targetId, Scene& scene) {
    auto& health = scene.get<Health>(targetId);
    health.current -= 25.0f;
    if (health.current <= 0.0f) {
        health.isDead    = true;
        health.lastDamager = projectileId;
    }
    scene.destroy(projectileId);  // deferred
}
```

---

## 8. Querying Entities

### Basic Component Query

Iterate all entities that possess a given set of components. The query resolves to all archetype chunks that are a superset of the requested component set.

```cpp
// Iterate every entity that has both Transform and RigidBody
scene.query<Transform, RigidBody>([](EntityId id, Transform& t, RigidBody& rb) {
    // Apply gravity manually if needed
    rb.linearVelocity.y -= 9.81f * fixedDt;
    t.position += rb.linearVelocity * fixedDt;
});
```

### Read-Only Query

Mark components as `const` to allow the scheduler to run the query in parallel with other read-only queries on the same component types.

```cpp
scene.query<const Health, const Name>([](EntityId id, const Health& h, const Name& n) {
    if (h.isDead) LOG_INFO("{} is dead", n.label);
});
```

### Filtered Query (Tag / Team)

```cpp
// Only entities on team 1
scene.query<Transform, const TeamTag>(
    [](EntityId id, Transform& t, const TeamTag& tag) {
        // process
    },
    QueryFilter{}.requireTag<TeamTag>([](const TeamTag& tag){ return tag.teamId == 1; })
);
```

### Single-Entity Access

When you already have an `EntityId` (e.g. from a collision callback or network message):

```cpp
if (scene.isValid(entityId)) {
    auto* health = scene.try_get<Health>(entityId);   // returns nullptr if not present
    if (health) health->current -= damage;
}
```

### Getting All Entities of an Archetype

```cpp
auto view = scene.view<game::PlayerArchetype>();
for (auto [id, transform, cc, camera] : view) {
    // ...
}
```

---

## 9. Entity Parenting and Transform Hierarchy

Parenting is used primarily to attach weapons to a player's hand socket, or to attach a muzzle-flash effect to a weapon barrel.

### Establishing a Parent

```cpp
// Attach a weapon entity to the player's hand socket
EntityId weaponId = factory.spawn<game::WeaponArchetype>(...);
EntityId handSocketId = player.getSocket("hand_r");  // socket = child entity baked into player rig

scene.setParent(weaponId, handSocketId);
// weaponId's Transform.parent is now set to handSocketId
```

### Transform Propagation

The `TransformSystem` runs before rendering each frame. It walks the hierarchy in depth-first order and concatenates local transforms to produce world-space `localToWorld` matrices.

```cpp
// TransformSystem pseudocode (simplified)
scene.query<Transform>([&](EntityId id, Transform& t) {
    if (t.parent == NULL_ENTITY) {
        t.localToWorld = Mat4::trs(t.position, t.rotation, t.scale);
    } else {
        const Mat4& parentWorld = scene.get<Transform>(t.parent).localToWorld;
        t.localToWorld = parentWorld * Mat4::trs(t.position, t.rotation, t.scale);
    }
});
```

### Detaching

```cpp
scene.clearParent(weaponId);  // weapon drops, now in world space at last computed position
```

### Socket Entities

A "socket" is just a named child entity with a `Transform` that is driven by the animation system. The naming convention is:

```
PlayerEntity
  └─ SocketEntity "hand_r"   (Transform driven by skeleton joint)
  └─ SocketEntity "hand_l"
  └─ SocketEntity "camera_pivot"
```

```cpp
// Look up a named socket child
EntityId find_socket(Scene& scene, EntityId parentId, std::string_view socketName) {
    for (EntityId child : scene.getChildren(parentId)) {
        if (scene.has<Name>(child)) {
            if (scene.get<Name>(child).label == socketName) return child;
        }
    }
    return NULL_ENTITY;
}
```

### Hierarchy Constraints

- Maximum hierarchy depth is 8 levels (engine limit, enforced at `setParent` time).
- Circular parenting is rejected with a logged error.
- Destroying a parent entity also destroys all descendants (depth-first, `OnDestroy` fires for each).
- `NetworkIdentity` components are not inherited; each networked entity in a hierarchy must have its own `NetworkIdentity`.
