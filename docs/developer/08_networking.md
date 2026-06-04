# 08 — Networking: Replication, Prediction, and RPCs

The engine uses a **server-authoritative** model. The server is the sole source of gameplay truth. Clients predict local player movement for zero-latency response and interpolate remote entities for smooth rendering.

## Network topology

```
[Client A] ──input──►  [Server]  ──snapshot──► [Client A]
[Client B] ──input──►  [Server]  ──snapshot──► [Client B]
                         │
                   authoritative PhysicsWorld + ECS World
```

Clients never communicate directly. All replication flows through the server.

## `NetworkIdentity` component

Every replicated entity has a `NetworkIdentity` component. Entities without it are purely local (particles, editor gizmos).

```cpp
// src/networking/public/networking/NetworkIdentity.h
namespace engine::networking {

struct NetworkIdentity {
    // kComponentId = 8
    uint32_t netId;                  // stable session-scoped ID; server-assigned
    uint32_t ownerClientId;          // client that sends InputFrames for this entity
                                     // SERVER_OWNED = 0xFFFFFFFF
    uint32_t replicatedComponents;   // bitmask of ReplicatedComponentBit
    float    relevanceRadius;        // metres; 0 = always relevant
};

} // namespace engine::networking
```

### Replicated component bits

```cpp
enum ReplicatedComponentBit : uint32_t {
    RCB_TRANSFORM       = (1u << 0),
    RCB_RIGID_BODY      = (1u << 1),
    RCB_HEALTH          = (1u << 2),
    RCB_WEAPON_STATE    = (1u << 3),
    RCB_ANIMATION_STATE = (1u << 4),
};
```

### Spawning a replicated entity (server)

```cpp
// EntityFactory assigns a NetId automatically when NetworkIdentity is present.
engine::core::ecs::SpawnParams params;
params.transform.position = spawnPoint;

engine::core::ecs::EntityId playerEid =
    ctx.entityFactory.spawn("PlayerEntity", params, ctx.world);

auto& net = ctx.world.getComponent<engine::networking::NetworkIdentity>(playerEid);
net.ownerClientId          = clientId;
net.replicatedComponents   = RCB_TRANSFORM | RCB_HEALTH | RCB_WEAPON_STATE;
net.relevanceRadius        = 300.f;  // metres
```

The engine broadcasts a `NetSpawn` reliable message to all clients within relevance range. Clients create a local entity and register it in `NetworkRegistry`.

## Snapshot system

The server builds a `WorldStateSnapshot` every 3 ticks (~20 Hz) and sends it to each client. Snapshots are **delta-compressed per client** against the last acknowledged snapshot. Unchanged component fields are omitted.

### Key timing parameters (configurable via `config/network.toml`)

```toml
[network]
snapshot_interval_ticks = 3          # how often to send snapshots (20 Hz at 64 Hz tick)
snapshot_ack_timeout_ms = 500        # full snapshot if no ACK within this window
max_entities_per_snapshot = 512
```

### Bandwidth budget (10-player example)

- ~300–600 bytes uncompressed per snapshot
- ~150–300 bytes after delta compression
- ~6–12 KB/s server-to-client at 20 Hz

## Client-side prediction

The local player's character controller runs speculatively on the client at the full 64 Hz rate without waiting for server confirmation. The engine stores an `InputFrame` history of the last 128 ticks (2 s).

### Reconciliation

When a server snapshot for tick `M` arrives:

1. If the client's predicted position at tick `M` is within the reconciliation threshold → no action.
2. If divergence exceeds the threshold:
   a. Overwrite the entity's state with the server value.
   b. Replay `InputFrame`s from `M+1` to `currentTick` through the same simulation systems.
   c. Blend the corrected position toward the visual position over `blend_frames` render ticks.

```toml
[network.prediction]
position_error_threshold_cm = 2.0    # reconcile if > 2 cm off
angle_error_threshold_deg   = 0.5
blend_frames                = 6
```

### Determinism requirements

For replay to produce the same result as the original tick, simulation systems must be deterministic:

- Do not read wall-clock time from `onGameTick` or system callbacks. Use `ctx.currentTick` for tick-seeded randomness.
- Do not issue side effects (audio, particle spawns) during replay. Check `tctx.isReplay`:

```cpp
void WeaponSystem(engine::core::ecs::World& world,
                  const engine::app::TickContext& tctx)
{
    world.query<...>([&](...) {
        if (inp.current.digitalJustPressed & kFireBit) {
            fireWeapon(...);
            if (!tctx.isReplay)
                audio.playOneShot("weapon_fire", pos);  // skip during replay
        }
    });
}
```

## Remote entity interpolation

Remote entities (other players, physics objects) are not predicted. They are interpolated between the last two received server snapshots, rendered 100 ms behind the current server time for smoothness.

The `SnapshotBuffer` handles this automatically. Game code does not need to manage interpolation manually — `Transform` components for remote entities are updated before the `Render` tick group runs.

```
Server snapshots:  [S_18]        [S_21]        [S_24]
Render time:           ──────────►
                             ↑ interpolating between S_18 and S_21
```

If no new snapshot arrives within 200 ms, the `SnapshotBuffer` extrapolates using the last known velocity. Beyond 200 ms the entity is hidden or frozen (game-configurable).

## RPCs (Remote Procedure Calls)

RPCs send a targeted reliable or unreliable call to one or more peers. Define them with `ENGINE_RPC`:

```cpp
// In your game headers — declares the RPC method and its wire registration
ENGINE_RPC(PlayerDied,
    engine::networking::RpcTarget::AllClients,
    engine::networking::RpcReliability::Reliable,
    (engine::core::ecs::EntityId victim, engine::core::ecs::EntityId killer));
```

Call site (server only):

```cpp
// Sends to all clients reliably
engine::networking::RPC::call<PlayerDiedRPC>(ctx.networkSystem, victimEid, killerEid);
```

Receive site (client):

```cpp
// Register in onInit
engine::networking::RPC::bind<PlayerDiedRPC>(
    [this, &ctx](engine::core::ecs::EntityId victim,
                 engine::core::ecs::EntityId killer)
    {
        ctx.eventBus.emit(engine::app::PlayerDiedEvent{ victim, killer });
        showKillFeed(victim, killer);
    });
```

### RPC targets

| Target | Who receives |
|--------|-------------|
| `Server` | The server only (from clients) |
| `AllClients` | Every connected client |
| `OwnerClient` | The client that owns the entity |
| `NearbyClients` | Clients within the entity's relevance radius |

### Reliability

| Mode | Channel | Use |
|------|---------|-----|
| `Reliable` | Guaranteed ordered delivery | Kill events, round state changes |
| `Unreliable` | Fire-and-forget | Fast-changing state already covered by snapshots |

Health, WeaponState, and AnimationState travel via the `UnreliableOrderedChannel` (newest-wins), not via RPCs, since the snapshot system already handles them.

## Checking network ownership in systems

Some server-side systems should only process entities owned by a specific client:

```cpp
world.query<engine::networking::NetworkIdentity,
            WeaponComponent>(
    [clientId](engine::core::ecs::EntityId eid,
               const engine::networking::NetworkIdentity& net,
               WeaponComponent& weapon)
    {
        if (net.ownerClientId != clientId) return;
        // ... process this client's weapon state
    });
```

Server-owned entities (`ownerClientId == SERVER_OWNED`) are never driven by `InputFrame` packets and run exclusively through server-side systems.

## Resolving a NetId to a local entity (client)

```cpp
engine::core::ecs::EntityId localEid =
    ctx.networkSystem.registry().resolve(incomingNetId);

if (localEid == engine::core::ecs::NULL_ENTITY) {
    // Entity not yet spawned locally — drop or buffer the message
    return;
}
```

## Next

[09 — Persistence](09_persistence.md): `SaveSystem`, `PlayerProfile`, `MatchRecord`, and server checkpoints.
