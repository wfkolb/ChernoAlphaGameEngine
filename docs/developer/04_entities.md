# 04 — Entities: `EntityFactory`, FPS Archetypes, and ECS

The engine uses an **archetype-based ECS**. An entity is a 64-bit handle with no data of its own; all data lives in components. Entities that share the same set of component types belong to the same archetype and are stored in contiguous SoA chunks for cache-efficient iteration.

## Core built-in components

| Component | Header | `kComponentId` | Description |
|-----------|--------|----------------|-------------|
| `Name` | `core/components/Name.h` | 0 | Editor display label (up to 64 chars inline) |
| `Transform` | `core/components/Transform.h` | 1 | World-space position/rotation/scale + cached `Mat4` |
| `InputReceiverComponent` | `core/input/InputReceiverComponent.h` | 2 | Marks entity as an input consumer; see §05 |
| `Health` | `core/components/Health.h` | 3 | `currentHp`, `maxHp`, `shieldPercent` |
| `Lifetime` | `core/components/Lifetime.h` | 4 | `remaining` (float); destroyed at zero by `LifetimeSystem` |
| `TeamTag` | `core/components/TeamTag.h` | 5 | `teamId` (uint8) |
| `RigidBody` | `physics/RigidBody.h` | 6 | Physics simulation; see §06 |
| `CharacterController` | `physics/CharacterController.h` | 7 | Capsule mover; see §06 |
| `NetworkIdentity` | `networking/NetworkIdentity.h` | 8 | Replication handle; see §08 |

Components must be registered in `Engine::init()` in ID order. The FPS components are registered for you by the engine when you call `registerFpsArchetypes`.

## `EntityFactory`

`EntityFactory` lets you name and register entity blueprints, then spawn them by name anywhere in your code.

```cpp
// src/core/public/core/ecs/EntityFactory.h
namespace engine::core::ecs {

struct SpawnParams {
    engine::core::math::Transform transform;  // world-space placement
    engine::core::ecs::EntityId   parent    = NULL_ENTITY;
    std::string_view              sceneName;  // "" = active scene
};

class EntityFactory {
public:
    // Register a blueprint. fn receives the new EntityId and a World reference.
    void registerArchetype(std::string_view name,
        std::function<void(EntityId, World&, const SpawnParams&)> fn);

    // Spawn by name. Returns NULL_ENTITY if the name is unknown.
    EntityId spawn(std::string_view name,
                   const SpawnParams& params,
                   World& world);
};

} // namespace engine::core::ecs
```

## FPS archetypes

`registerFpsArchetypes()` registers the seven standard FPS entity blueprints. Call it once in `IGame::onInit`.

```cpp
#include <core/fps/FpsArchetypes.h>

void ArenaGame::onInit(engine::app::GameContext& ctx) {
    engine::core::fps::registerFpsArchetypes(ctx.entityFactory);
}
```

Registered names and their default component sets:

| Name | Components | Notes |
|------|-----------|-------|
| `PlayerEntity` | Transform, Health, CharacterController, InputReceiver, NetworkIdentity, TeamTag, Name | Player character |
| `WeaponEntity` | Transform, Name | Held weapon; parented to player |
| `ProjectileEntity` | Transform, RigidBody, Collider, Lifetime, NetworkIdentity, Name | Grenades, rockets |
| `StaticPropEntity` | Transform, RigidBody(static), Collider, Mesh, Material, Name | Level furniture |
| `TriggerEntity` | Transform, Collider(trigger), Name | Overlap events |
| `SpawnPointEntity` | Transform, TeamTag, Name | Designates a spawn location |
| `PickupEntity` | Transform, Collider(trigger), Lifetime, NetworkIdentity, Name | Health/ammo pickups |

## Spawning entities

```cpp
// Spawn a player at a given transform
engine::core::ecs::SpawnParams params;
params.transform.position = spawnPoint;
params.transform.rotation = engine::core::math::Quat::identity();

engine::core::ecs::EntityId playerEid =
    ctx.entityFactory.spawn("PlayerEntity", params, ctx.world);

// Set initial health
auto& hp  = ctx.world.getComponent<engine::core::Health>(playerEid);
hp.currentHp = hp.maxHp = 100.f;

// Assign team
auto& tag = ctx.world.getComponent<engine::core::TeamTag>(playerEid);
tag.teamId = assignTeam(pid);

// Link to the player's network identity
auto& net = ctx.world.getComponent<engine::networking::NetworkIdentity>(playerEid);
net.ownerClientId = pid;
```

## Writing a custom archetype

Register an archetype from your game code if the standard FPS set doesn't cover your needs:

```cpp
ctx.entityFactory.registerArchetype("BombSite",
    [](engine::core::ecs::EntityId eid,
       engine::core::ecs::World& world,
       const engine::core::ecs::SpawnParams& p)
    {
        world.addComponent<engine::core::math::Transform>(eid, { p.transform });
        world.addComponent<engine::physics::Collider>(eid, {
            .shape   = engine::physics::ColliderShape::box({ 2.f, 0.5f, 2.f }),
            .trigger = true
        });
        world.addComponent<engine::core::Name>(eid, { "BombSite" });
        world.addComponent<BombSiteComponent>(eid, {});
    });
```

## Querying entities

Use `World::query<T...>` to iterate all entities that have a given set of components:

```cpp
// Move all projectiles every tick
void projectileSystem(engine::core::ecs::World& world, float dt) {
    world.query<engine::core::math::Transform,
                engine::physics::RigidBody>(
        [dt](engine::core::ecs::EntityId,
             engine::core::math::Transform& xform,
             const engine::physics::RigidBody& rb)
        {
            xform.position += rb.linearVelocity * dt;
        });
}
```

## Deferring structural changes

Adding or removing components during a query is unsafe because it would invalidate the chunk iterator. Defer these operations with `CommandBuffer`:

```cpp
engine::core::ecs::CommandBuffer cmd;

world.query<engine::core::Lifetime>(
    [&cmd](engine::core::ecs::EntityId eid,
           const engine::core::Lifetime& lt)
    {
        if (lt.remaining <= 0.f)
            cmd.destroyEntity(eid);
    });

cmd.flush(world);  // apply all deferred operations after the query
```

## Entity parenting

Setting `SpawnParams::parent` attaches the entity to a parent's Transform hierarchy. The `TransformSystem` rebuilds the local-to-world matrix each tick:

```cpp
// Attach a weapon model to a player's weapon socket
engine::core::ecs::SpawnParams weaponParams;
weaponParams.parent    = playerEid;
weaponParams.transform = weaponSocketLocalTransform;

engine::core::ecs::EntityId weaponEid =
    ctx.entityFactory.spawn("WeaponEntity", weaponParams, ctx.world);
```

## Next

[05 — Input](05_input.md): Binding actions, reading input in systems, and the networked `InputFrame`.
