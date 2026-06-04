# 07 — Combat: `DamageSystem`, Hitscan, and `EventBus`

Combat in the engine is **server-authoritative**: the client sends a fire intent via its `InputFrame`, the server validates the hitscan, applies damage, and replicates the result. The client plays visual/audio feedback speculatively but never modifies `Health` directly.

## Architecture overview

```
Client fires
  └─► InputFrame (digitalJustPressed bit "Fire") → sent to server each tick

Server receives InputFrame
  └─► WeaponSystem (PrePhysics)
        └─► builds HitscanValidationRequest, queues it

DamageSystem (PostPhysics, server only)
  └─► dedup by fireSerial
  └─► raycast via PhysicsWorld (optionally with lag compensation — Phase 7 placeholder)
  └─► applies CommandBuffer::Delta<Health>
  └─► emits HitscanHitEvent on EventBus
  └─► if kill: sends PlayerDied RPC (reliable) to all clients
```

## `HitscanValidationRequest`

The `WeaponSystem` fills this struct and submits it to the `DamageSystem` queue:

```cpp
// src/networking/public/networking/HitscanValidationRequest.h
namespace engine::networking {

struct HitscanValidationRequest {
    uint32_t fireSerial;            // monotonically increasing per weapon, per owner
    uint32_t clientTick;            // client's tick when the trigger was pulled
    engine::core::math::Vec3 rayOrigin;
    engine::core::math::Vec3 rayDirection;
    uint32_t targetNetId;           // hint from client (used for fast-path, not trusted)
};

} // namespace engine::networking
```

`fireSerial` is used for deduplication: if the same serial arrives twice (e.g. reliable retransmit), the second copy is silently dropped.

## `DamageSystem`

The `DamageSystem` lives in `src/app/` and runs on the server only. It consumes the `HitscanValidationRequest` queue and applies `Health` deltas via the ECS `CommandBuffer`.

```cpp
// src/app/DamageSystem.h
namespace engine::app {

class DamageSystem {
public:
    explicit DamageSystem(engine::physics::PhysicsWorld& physics,
                          engine::networking::ReplicationSystem& replication,
                          engine::core::EventBus& events);

    // Enqueue a request (called from WeaponSystem, PrePhysics).
    void enqueue(const engine::networking::HitscanValidationRequest& req,
                 engine::core::ecs::EntityId shooterEid);

    // Process all queued requests (called from PostPhysics).
    void flush(engine::core::ecs::World& world,
               engine::core::ecs::CommandBuffer& cmd);
};

} // namespace engine::app
```

### Wiring the system

```cpp
// In ArenaGame::onInit
ctx.scheduler.registerSystem(engine::app::TickGroup::PostPhysics,
    [this, &ctx](float) {
        engine::core::ecs::CommandBuffer cmd;
        damageSystem_.flush(ctx.world, cmd);
        cmd.flush(ctx.world);
    });
```

### How damage is applied

`DamageSystem` uses `CommandBuffer::Delta<Health>` rather than setting absolute HP values. This ensures that multiple simultaneous hits (shotgun pellets, multi-projectile explosions) commute correctly — each is an independent subtraction:

```cpp
// Inside DamageSystem::flush — engine-internal detail shown for context
cmd.applyDelta<engine::core::Health>(targetEid,
    [amount](engine::core::Health& hp) {
        hp.currentHp = std::max(0.f, hp.currentHp - amount);
    });
```

Do **not** apply damage by directly setting `hp.currentHp` from game code; always use the `DamageSystem` queue so that deduplication and lag compensation validation run correctly.

## Responding to kill events

Subscribe to `HitscanHitEvent` and `PlayerDiedEvent` on the `EventBus`:

```cpp
void ArenaGame::onInit(engine::app::GameContext& ctx) {
    ctx.eventBus.subscribe<engine::app::HitscanHitEvent>(
        [this, &ctx](const engine::app::HitscanHitEvent& e) {
            // Award assist credit if the target later dies
            trackAssist(e.shooter, e.target, e.damage);
        });

    ctx.eventBus.subscribe<engine::app::PlayerDiedEvent>(
        [this, &ctx](const engine::app::PlayerDiedEvent& e) {
            gameMode_.onPlayerDeath(ctx, e.victim, e.killer);
        });
}
```

`PlayerDiedEvent` is fired on both the server and all clients (the server emits it locally and sends a `PlayerDied` reliable RPC).

## `EventBus`

`EventBus` is a lightweight synchronous event dispatcher. All subscribers are called immediately in the thread that calls `emit()`.

```cpp
// Emit — called during a game tick
ctx.eventBus.emit(engine::app::PlayerDiedEvent{
    .victim = victimEid,
    .killer = killerEid,
});

// Subscribe — typically called in onInit
ctx.eventBus.subscribe<engine::app::PlayerDiedEvent>(
    [](const engine::app::PlayerDiedEvent& e) { /* ... */ });

// Unsubscribe by handle (if needed at cleanup)
auto handle = ctx.eventBus.subscribe<SomeEvent>(...);
ctx.eventBus.unsubscribe(handle);
```

Events are fired on the game thread and resolved before the tick returns. Do not emit events from background threads; queue them and emit during `onGameTick` instead.

## Defining custom events

```cpp
// MyEvents.h
struct BombPlantedEvent {
    engine::core::ecs::EntityId planterEid;
    engine::core::ecs::EntityId bombSiteEid;
    float plantProgress = 0.f;
};

// Subscribe
ctx.eventBus.subscribe<BombPlantedEvent>(
    [](const BombPlantedEvent& e) {
        // start bomb-defuse countdown, update HUD, etc.
    });

// Emit
ctx.eventBus.emit(BombPlantedEvent{
    .planterEid  = playerEid,
    .bombSiteEid = siteEid,
    .plantProgress = 0.f,
});
```

## Implementing a weapon that fires

```cpp
void WeaponSystem(engine::core::ecs::World& world,
                  engine::app::DamageSystem& dmgSystem,
                  uint32_t currentTick)
{
    world.query<engine::networking::NetworkedInputComponent,
                WeaponComponent,
                engine::core::math::Transform>(
        [&](engine::core::ecs::EntityId shooterEid,
            const engine::networking::NetworkedInputComponent& inp,
            WeaponComponent& weapon,
            const engine::core::math::Transform& xform)
        {
            if (!(inp.current.digitalJustPressed & kFireBit)) return;
            if (weapon.ammo == 0 || weapon.isReloading)         return;

            --weapon.ammo;

            engine::networking::HitscanValidationRequest req;
            req.fireSerial    = weapon.nextFireSerial++;
            req.clientTick    = inp.current.tick;
            req.rayOrigin     = xform.position + kMuzzleOffset;
            req.rayDirection  = lookDirection(inp.current);
            req.targetNetId   = 0;   // 0 = no client hint

            dmgSystem.enqueue(req, shooterEid);
        });
}
```

## Lag compensation

Phase 7 ships with a **placeholder** lag-compensation ring buffer. The `DamageSystem` performs raycasts against current world state, not the rewound state from the client's view time. Full rewind is a post-Phase-7 addition. The ring buffer infrastructure is in place; filling it with historical transforms is left as a future task.

## Next

[08 — Networking](08_networking.md): Replication, client-side prediction, interpolation, and RPCs.
