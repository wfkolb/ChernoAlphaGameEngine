# Networking: Gameplay Object Replication

**Module:** `engine::networking`
**Phase:** 6
**Task:** 09
**Author:** Networking Lead
**Cross-references:**
- `task-07-gameplay-loop.md` — WorldState snapshot structure and the 64-tick server loop (Project Lead)
- `task-03-ecs.md` through `task-06-physics.md` — ECS archetype design, entity lifetime, scene graph, physics integration (Physics+Scene Lead)

---

## Table of Contents

1. [Replication Model Overview](#1-replication-model-overview)
2. [NetworkIdentity Component](#2-networkidentity-component)
3. [Replicated Components](#3-replicated-components)
4. [WorldState Snapshot](#4-worldstate-snapshot)
5. [Client-Side Prediction (Local Player)](#5-client-side-prediction-local-player)
6. [Remote Entity Interpolation](#6-remote-entity-interpolation)
7. [Entity Spawn and Destroy Replication](#7-entity-spawn-and-destroy-replication)
8. [RPC (Remote Procedure Calls)](#8-rpc-remote-procedure-calls)
9. [Physics State Replication](#9-physics-state-replication)
10. [Interest Management / Relevance](#10-interest-management--relevance)
11. [Bandwidth Budget and Priorities](#11-bandwidth-budget-and-priorities)
12. [Wire Format Reference](#12-wire-format-reference)
13. [Implementation Notes and Gotchas](#13-implementation-notes-and-gotchas)

---

## 1. Replication Model Overview

The engine uses a **server-authoritative** replication model. The server is the single source of truth for all gameplay state. Clients are display terminals and input sources — they render interpolated/predicted state but never make unilateral gameplay decisions.

### Core Principles

| Principle | Description |
|-----------|-------------|
| Server authority | All entity state transitions (damage, death, movement, physics) are decided by the server |
| Clients as peers | Each client receives state updates and sends input; clients do not send state directly to each other |
| Prediction permitted | The local player's character controller may predict movement client-side, but the server corrects any divergence |
| Determinism expected | CharacterController and core physics are expected to be deterministic given the same inputs; this is what makes prediction + reconciliation tractable |

### Network Topology

```
  [Client A] ---input---> [Server] ---snapshot---> [Client A]
  [Client B] ---input---> [Server] ---snapshot---> [Client B]
                             |
                       authoritative
                       PhysicsWorld
                       ECS World
                       GameMode
```

Clients never communicate directly. All replication flows through the server.

### NetId

Every replicated entity has a stable `uint32_t` **NetId** assigned by the server at spawn time. The NetId is the shared handle across all peers — server and all clients refer to the same logical entity using the same NetId, even though the underlying ECS entity handle (archetype slot index) may differ per-machine.

NetIds are **never reused** within a session. A monotonically increasing counter is maintained on the server. After an entity is destroyed, its former NetId is retired for the remainder of the session, preventing stale-message aliasing.

### Ownership

Each replicated entity has an **owning ClientId** (`uint32_t ownerClientId`). The server uses this to determine which client's input stream to consume for that entity. The special sentinel value `SERVER_OWNED` (defined as `0xFFFFFFFF`) marks entities controlled by server logic (e.g. AI, static physics objects, game-mode managers).

Only the owning client sends `InputFrame` messages for an entity. Other clients may receive that entity's state but cannot influence it directly.

---

## 2. NetworkIdentity Component

`NetworkIdentity` is an ECS component attached to every entity that participates in replication. Entities without this component are purely local (e.g. particle emitters, UI anchors).

### Component Fields

```cpp
// engine/networking/NetworkIdentity.h
namespace engine::networking {

struct NetworkIdentity {
    uint32_t netId;                  // Stable session-scoped identifier; server-assigned
    uint32_t ownerClientId;          // CLIENT_ID or SERVER_OWNED (0xFFFFFFFF)
    uint32_t replicatedComponents;   // Bitmask; see ReplicatedComponentBit enum
    float    relevanceRadius;        // Metres; 0.0f = always relevant (no culling)
};

} // namespace engine::networking
```

**Field details:**

- `netId` — Assigned by `NetworkIdAllocator` on the server; written once at spawn; read-only on clients.
- `ownerClientId` — Used server-side to route incoming `InputFrame` packets to the correct entity. On clients it is informational (determines whether local prediction runs for this entity).
- `replicatedComponents` — A bitmask whose bits correspond to `ReplicatedComponentBit` entries. This is the **maximum** set of components that will ever be sent for this entity. Per-snapshot dirty tracking is layered on top (Section 3).
- `relevanceRadius` — The server's interest management system (Section 10) uses this to decide which clients receive updates for this entity. A player character might have a radius of 300 m; a small physics prop 50 m.

### NetId Allocation (Server)

```cpp
// engine/networking/NetworkIdAllocator.h
class NetworkIdAllocator {
public:
    uint32_t Allocate() {
        return m_nextId++;          // Monotonically increasing; never wraps in normal sessions
    }
private:
    std::atomic<uint32_t> m_nextId{1}; // 0 is reserved as INVALID_NET_ID
};
```

The allocator lives on the server only. There is no coordination needed — the single server process assigns all NetIds. The counter starts at 1; `0` is `INVALID_NET_ID`.

### Client Registration

When a client receives a `NetSpawn` message (Section 7), it calls `NetworkRegistry::Register`:

```cpp
// engine/networking/NetworkRegistry.h (client-side)
class NetworkRegistry {
public:
    // Map NetId -> local ECS EntityHandle
    void Register(uint32_t netId, ecs::EntityHandle localHandle);
    void Unregister(uint32_t netId);
    ecs::EntityHandle Resolve(uint32_t netId) const; // returns INVALID_ENTITY if not found
private:
    std::unordered_map<uint32_t, ecs::EntityHandle> m_netToLocal;
};
```

On receiving snapshot data for a known NetId, the client resolves the local entity handle and applies component updates in place. Snapshots for unknown NetIds are discarded with a warning (the `NetSpawn` reliable message is expected to arrive first, but packet reordering is handled by the reliability layer).

---

## 3. Replicated Components

### Which Components Are Replicated

The `ReplicatedComponentBit` enum lists every component eligible for replication. This is the authoritative list; adding a new replicated component requires a new bit here and a corresponding serializer registered in `ComponentSerializer`.

```cpp
enum ReplicatedComponentBit : uint32_t {
    RCB_TRANSFORM       = (1u << 0),  // Position, rotation, scale
    RCB_RIGID_BODY      = (1u << 1),  // Linear vel, angular vel, sleep state
    RCB_HEALTH          = (1u << 2),  // Current HP, max HP, shield
    RCB_WEAPON_STATE    = (1u << 3),  // Equipped weapon, ammo, fire mode, reload progress
    RCB_ANIMATION_STATE = (1u << 4),  // (stub) animation clip ID, blend weight, play position
    // Bits 5–31 reserved for future components
};
```

### Non-Replicated Components

The following components exist only in the local ECS and are never serialized to the wire:

| Component | Reason |
|-----------|--------|
| `InputReceiver` | Per-client; input is sent as `InputFrame` packets, not replicated component state |
| `Camera` | View is purely local; the server has no concept of a camera |
| `ParticleEmitter` | Client-side visual effect; driven by replicated events (RPC), not state |
| `AudioSource` | Same as above |
| `UIAnchor` | Local HUD; no server relevance |
| `LightComponent` | Rendering-only; driven off replicated Transform if needed |

### Per-Component Replication Rate

| Component | Rate | Rationale |
|-----------|------|-----------|
| `Transform` | Every snapshot (20 Hz) | Position and orientation must stay current; drives rendering and interpolation |
| `RigidBody` (velocities) | Every snapshot for active bodies; skipped when sleeping | Velocity needed for client-side extrapolation |
| `Health` | On change only | Low frequency; value changes only on damage/heal events |
| `WeaponState` | On change only | Changes on fire, reload, weapon switch |
| `AnimationState` | On change only (stub) | Clip transitions are infrequent |

### Delta Compression

The server tracks the **last ACKed snapshot number** per client (Section 4). For each snapshot being built, a **dirty component mask** is computed per entity:

```
dirtyMask = currentComponentData XOR dataAtLastAckedSnapshot
```

Only components with changed data are included in the snapshot payload for that entity. If no components changed for an entity, the entity record is omitted entirely from the snapshot.

This means:
- A stationary, sleeping rigid body with full health will not appear in any snapshot until its state changes.
- A moving player character will appear in every snapshot (Transform dirty every tick).

The dirty mask is a `uint8_t` (bits 0–4 used; bits 5–7 reserved) sent in the per-entity header. See Section 12 for wire layout.

---

## 4. WorldState Snapshot

> The gameplay loop that drives snapshot production is documented in `task-07-gameplay-loop.md`. This section covers the snapshot's network representation and delivery.

### Snapshot Cadence

The server runs at **64 Hz** (fixed timestep ~15.6 ms). Snapshots are sent to clients at **~20 Hz** — every 3 ticks. This is a deliberate bandwidth/latency trade-off; 20 Hz is standard for fast-paced FPS titles.

```
Tick: 0  1  2  3  4  5  6  7  8  9 ...
Snap: S        S        S        S    (every 3 ticks)
```

### Snapshot Structure

```
SnapshotPacket {
    Header {
        uint8_t  packetType        // PACKET_SNAPSHOT = 0x02
        uint32_t snapshotTick      // Server tick number this snapshot represents
        uint64_t serverTimestampUs // Microseconds since session start (for interpolation)
        uint16_t entityCount       // Number of entity records in this snapshot
        uint32_t baselineTick      // Tick of the last ACKed snapshot (delta baseline); 0 = full snapshot
    }
    EntityRecord[entityCount] {
        uint32_t netId
        uint8_t  dirtyComponentMask
        // Followed by serialized data for each set bit in dirtyComponentMask
        // Variable length; see Section 12 for per-component byte counts
    }
}
```

All multi-byte integers are **little-endian**. Floats are IEEE 754 single-precision (4 bytes) unless quantized (see Section 12).

### Snapshot ACK

Clients send `SnapshotAck` packets back to the server after processing each received snapshot:

```
SnapshotAckPacket {
    uint8_t  packetType    // PACKET_SNAPSHOT_ACK = 0x03
    uint32_t snapshotTick  // The tick number being acknowledged
}
```

The server maintains a `PerClientState` record with `lastAckedSnapshotTick`. When building the next snapshot for a client, delta compression uses `lastAckedSnapshotTick` as the baseline. If no ACK has been received (new client or packet loss streak), the server falls back to sending a full snapshot (`baselineTick = 0` in the header).

### Snapshot Retention

The server keeps a ring buffer of the last `SNAPSHOT_HISTORY_SIZE` (32) snapshots to service delta baselines. This covers ~1.6 seconds of history at 20 Hz — enough to handle typical packet loss and RTT without falling back to full snapshots.

---

## 5. Client-Side Prediction (Local Player)

Client-side prediction (CSP) eliminates the perceived input lag that would otherwise result from the client waiting for the server to process its inputs. The local player's character controller runs speculatively on the client; the server's authoritative result is used to correct any divergence.

### Input Frame

Every tick (64 Hz) the client constructs and transmits an `InputFrame`:

```cpp
struct InputFrame {
    uint32_t tick;          // Client-side tick counter (aligned to server via clock sync)
    float    moveDirX;      // Normalized [-1, 1]; world-space strafe
    float    moveDirZ;      // Normalized [-1, 1]; world-space forward
    float    yaw;           // Camera yaw in radians
    float    pitch;         // Camera pitch in radians (clamped ±89°)
    bool     jumpPressed;   // True if jump input active this tick
    bool     firePressed;   // True if primary fire active this tick
    bool     adsPressed;    // True if aim-down-sights active this tick
    uint8_t  _pad;          // Alignment
};
```

Input frames are sent unreliably (UDP) every tick. They are **not** individually acknowledged. The server processes each frame as it arrives and uses the `tick` field to discard stale inputs (server only processes input frames with tick >= last processed tick for that client).

### Prediction Loop (Client)

```
each tick on client:
  1. Construct InputFrame(tick=currentTick, ...)
  2. Transmit InputFrame to server (unreliable)
  3. Apply InputFrame to local CharacterController (predict)
  4. Store {InputFrame, predictedState} in PredictionBuffer[currentTick % BUFFER_SIZE]
  5. Advance currentTick
```

The `PredictionBuffer` is a circular buffer with `BUFFER_SIZE = 128` slots (covers 2 seconds at 64 Hz, well beyond any expected RTT).

### Server Processing

```
each tick on server (per client entity):
  1. Dequeue latest InputFrame from that client's input queue
  2. Run CharacterController::Step(inputFrame, dt) using identical logic to client
  3. Store authoritative result in entity's Transform and RigidBody components
  4. Include entity in next snapshot
```

The server runs the **same** `CharacterController` implementation as the client. Divergence should be rare; it occurs when:
- A physics force was applied server-side (explosion knockback, etc.)
- The client's clock is skewed and it sent the wrong tick number
- Non-determinism in floating-point (edge case, rare)

### Reconciliation

When the client receives a snapshot containing the authoritative state for its local player:

```
on snapshot received (client):
  serverPos   = snapshot.entityRecord[localPlayerNetId].transform.position
  serverTick  = snapshot.snapshotTick

  predictedPos = PredictionBuffer[serverTick % BUFFER_SIZE].predictedState.position

  error = length(serverPos - predictedPos)

  if error > RECONCILIATION_THRESHOLD (0.05 m):
      // 1. Teleport to authoritative position
      localPlayer.transform.position = serverPos

      // 2. Replay all buffered inputs after serverTick
      for t = serverTick + 1 to currentTick:
          CharacterController::Step(PredictionBuffer[t].inputFrame, dt)
```

The reconciliation threshold (`0.05 m`, i.e. 5 cm) is tunable. Below this threshold the error is treated as imperceptible and silently discarded to avoid jitter from floating-point noise.

After replay the client is back in sync with the server's last known state while remaining at the current (predicted) position for the current frame.

---

## 6. Remote Entity Interpolation

Remote players and physics-driven objects are **not predicted** on the client. Predicting another player's movement would require guessing their input, which is unreliable. Instead, remote entities are rendered from a **delayed and interpolated** view of received snapshots.

### Snapshot Buffer

Each client maintains a `SnapshotBuffer` per remote entity (or one global buffer indexed by NetId):

```
SnapshotBuffer {
    CircularBuffer<SnapshotSample, N=4> samples;
    // Each sample: { serverTimestampUs, position, rotation, velocity }
}
```

The buffer stores the **last 4** received snapshots for each remote entity. Only snapshots in which that entity appeared (i.e. it was dirty or a full snapshot) produce a sample.

### Interpolation Target Time

```
renderTime = currentWallClockUs - interpolationDelayUs
```

`interpolationDelayUs` is typically **100 ms** (two snapshot intervals at 20 Hz). This ensures the render time falls between two buffered samples in almost all cases.

The render time is computed per-frame (not per-tick), so interpolation is smooth at any display framerate.

### Interpolation Logic

```
given renderTime:
  find samples A, B such that A.timestamp <= renderTime <= B.timestamp

  t = (renderTime - A.timestamp) / (B.timestamp - A.timestamp)  // [0, 1]

  renderedPosition = lerp(A.position, B.position, t)
  renderedRotation = slerp(A.rotation, B.rotation, t)
  renderedVelocity = lerp(A.velocity, B.velocity, t)  // for animation blending
```

If `renderTime` is older than the oldest sample (buffer underrun — packet burst or loss), the oldest sample is used (no extrapolation; extrapolation produces artifacts on direction changes).

If `renderTime` is newer than the newest sample (buffer overrun — not enough snapshots received), the newest sample is held and **linear extrapolation** is applied for up to **200 ms** before snapping to the last known position. Extrapolation beyond 200 ms is capped to avoid entities flying off into the distance.

### Trade-offs

Remote entities appear approximately **100 ms in the past** relative to the local player's rendered frame. This is an accepted trade-off for smooth movement. In competitive FPS design, lag compensation on the server (using stored snapshot history to do hit detection at the time of the client's fire input) compensates for this offset. Lag compensation is noted here as a Phase 7 item and is not implemented in Phase 6.

---

## 7. Entity Spawn and Destroy Replication

Spawn and destroy messages are sent over the **reliable channel** of the custom UDP transport (Section 8 of `task-08-transport.md`). They must arrive and are retransmitted until ACKed.

### NetSpawn Message

```
NetSpawnPacket {
    uint8_t  packetType       // PACKET_NET_SPAWN = 0x10
    uint32_t netId            // Newly allocated NetId (server-assigned)
    uint32_t ownerClientId    // Owner; SERVER_OWNED = 0xFFFFFFFF
    uint16_t archetypeNameLen // Byte length of the archetype name string
    char[]   archetypeName    // UTF-8 archetype name (not null-terminated)
    uint32_t replicatedComponents  // Initial replicatedComponents bitmask
    float    relevanceRadius  // Initial relevance radius
    // Followed by full component data for every set bit (same format as snapshot EntityRecord)
}
```

The `archetypeName` refers to an archetype registered in the ECS archetype table (see `task-03-ecs.md`). Clients look up the archetype to create the entity with the correct component layout before applying `initialComponentData`.

On receiving `NetSpawn`, the client:
1. Looks up `archetypeName` in `ArchetypeRegistry`.
2. Creates a new local entity with that archetype.
3. Deserializes `initialComponentData` into the entity's components.
4. Sets `NetworkIdentity.netId`, `.ownerClientId`, `.replicatedComponents`, `.relevanceRadius`.
5. Calls `NetworkRegistry::Register(netId, localHandle)`.
6. If `ownerClientId == localClientId`: activates `InputReceiver`, `Camera`, CSP prediction.

### NetDestroy Message

```
NetDestroyPacket {
    uint8_t  packetType  // PACKET_NET_DESTROY = 0x11
    uint32_t netId       // NetId of entity to remove
}
```

On receiving `NetDestroy`, the client:
1. Resolves `netId` to `localHandle` via `NetworkRegistry`.
2. Destroys the local ECS entity (deferred to end-of-frame via `EntityCommandBuffer`).
3. Calls `NetworkRegistry::Unregister(netId)`.

Any subsequent snapshot data for that NetId is silently discarded.

### Late-Join Handling

When a client connects mid-session or reconnects after a drop:

1. Server pauses sending normal snapshots to that client.
2. Server sends a **full scene snapshot**: one `NetSpawn` message per currently-live replicated entity, in NetId order.
3. After all spawns are ACKed, the server sends a `LateJoinComplete` reliable message.
4. Client transitions from `STATE_LOADING` to `STATE_PLAYING`; normal snapshot stream begins.

Full scene snapshots can be large (hundreds of entities). The reliable transport handles fragmentation. The client must not begin simulation until `LateJoinComplete` is received; displaying a loading screen during this window is required.

---

## 8. RPC (Remote Procedure Calls)

RPCs are one-shot function calls invoked on a remote peer. They are distinct from snapshot replication: snapshots replicate *state*, RPCs trigger *events*.

### Delivery Modes

| Mode | Mechanism | Use Cases |
|------|-----------|-----------|
| **Unreliable** | Single UDP datagram; no retry | Hit sparks, footstep sounds, shell casings, muzzle flash sync |
| **Reliable** | Retransmitted until ACKed (same ACK machinery as reliable messages) | Round start/end, player respawn, score update, game-mode state changes |

Unreliable RPCs that are lost produce minor visual glitches; they are not worth the bandwidth overhead of reliability for effects that are imperceptible if missed.

Reliable RPCs that are lost would cause persistent gameplay state desync; they must be retransmitted.

### RPC Targets

| Target | Description |
|--------|-------------|
| `Server` | Called on a client; executes on the server only |
| `AllClients` | Called on the server; broadcast to every connected client |
| `OwnerClient` | Called on the server; sent only to the entity's owning client |
| `NearbyClients` | Called on the server; sent to clients within `relevanceRadius` of the entity |

### RPC Definition and Dispatch

RPCs are declared using a macro that registers them in a compile-time dispatch table:

```cpp
// Example declaration (in a header visible to both server and client):
ENGINE_RPC(PlayerRespawn, Reliable, AllClients,
    uint32_t netId,
    Vec3     spawnPosition,
    float    spawnYaw
)

ENGINE_RPC(HitEffect, Unreliable, NearbyClients,
    uint32_t hitEntityNetId,
    Vec3     hitPoint,
    Vec3     hitNormal,
    uint8_t  weaponType
)
```

The `ENGINE_RPC` macro generates:
- A serialization function (writes arguments to a `BitStream`).
- A deserialization function (reads from `BitStream`, calls the registered handler).
- A dispatch table entry keyed on a `uint16_t` RPC ID (hash of the name, collision-checked at startup).

```
RpcPacket {
    uint8_t  packetType   // PACKET_RPC_UNRELIABLE = 0x20 or PACKET_RPC_RELIABLE = 0x21
    uint16_t rpcId        // Identifies which RPC to dispatch
    uint32_t senderNetId  // NetId of the instigating entity (or INVALID_NET_ID)
    uint16_t payloadLen   // Bytes of serialized argument data
    uint8_t  payload[]    // Serialized arguments
}
```

On receipt, the peer looks up `rpcId` in the dispatch table, deserializes `payload`, and calls the registered C++ handler. If `rpcId` is unknown (version mismatch), the packet is logged and dropped.

---

## 9. Physics State Replication

> This section is co-authored with the Physics+Scene Lead and should be read alongside `task-05-physics.md` (PhysicsWorld design) and `task-06-scene.md` (scene instance management).

### Server as Physics Authority

The server runs `PhysicsWorld::Step(dt)` at **64 Hz** before snapshot generation. All physics results (updated transforms, velocities, collision responses) are authoritative. Clients receive physics state through snapshots and must not run physics independently for server-owned or remote entities.

```
Server frame order (per tick):
  1. Consume input frames (client inputs applied to character controllers)
  2. PhysicsWorld::Step(dt = 1/64 s)
  3. Game logic (damage, pickups, game-mode rules)
  4. If (tick % 3 == 0): build and send WorldState snapshot
```

### Rigid Body Classification and Replication Policy

| Body Type | Transform Replicated | Velocity Replicated | Notes |
|-----------|---------------------|--------------------|----|
| **Dynamic** (active) | Yes, every snapshot | Yes, every snapshot | Frequent; delta-compressed |
| **Dynamic** (sleeping) | No (omitted) | No (omitted) | Woken by physics event → resumed replication |
| **Static** | Never | Never | Identical on all peers from scene load |
| **Kinematic** | Yes, every snapshot | No | Server moves kinematic bodies explicitly; clients follow transform |

Dynamic bodies that transition from sleeping to active generate a `PhysicsWake` RPC (unreliable) in addition to resuming snapshot inclusion, so clients can play a wake-up animation/sound.

### Character Controller (Local Player)

The local player's `CharacterController` is the only physics object clients simulate authoritatively. Its replication in the snapshot includes:

```
CharacterControllerState {
    Vec3  position        // 12 bytes (3 × float32)
    Vec3  velocity        // 12 bytes
    float yaw             // 4 bytes
    bool  onGround        // 1 byte
    uint8_t moveState     // 1 byte (enum: IDLE, WALKING, RUNNING, AIRBORNE, CROUCHING)
    uint8_t _pad[2]       // Alignment
}  // Total: 32 bytes
```

This is transmitted as part of the `RCB_TRANSFORM` and `RCB_RIGID_BODY` bits. `onGround` and `moveState` are packed into the `RCB_RIGID_BODY` payload for the character entity specifically (detected by archetype tag `ARCHETYPE_TAG_CHARACTER`).

### Physics Correction for the Local Player

When a snapshot arrives carrying authoritative `CharacterControllerState` for the local player:

```
correctionError = length(serverPosition - predictedPosition)

if correctionError > PHYSICS_CORRECTION_THRESHOLD (0.05 m):
    // Hard correction
    characterController.position = serverPosition
    characterController.velocity = serverVelocity
    characterController.onGround = serverOnGround

    // Replay all buffered inputs from serverTick+1 to currentTick
    ReplayPredictionBuffer(serverTick + 1, currentTick)
```

The `PHYSICS_CORRECTION_THRESHOLD` of 5 cm is chosen to absorb floating-point noise in the deterministic simulation while catching genuine prediction errors (e.g. server applied knockback the client didn't know about).

Visual smoothing: after a correction, the rendered position is smoothed over 3–5 frames using `lerp(renderedPos, simulatedPos, smoothAlpha)` to avoid a visible snap to the observer.

### Remote Player Physics

Remote players are **never predicted** on a client. Their `CharacterControllerState` is delivered via snapshots and rendered through the interpolation system (Section 6). Clients maintain a `SnapshotBuffer` per remote character, interpolating position, velocity, yaw, and `moveState` (the latter drives animation blending).

### Multi-Client Shared Scene Convergence

When two or more clients inhabit the same scene instance:

```
Client A's perspective:
  - Client A's character: locally predicted, server-reconciled
  - Client B's character: interpolated from snapshots (100 ms behind)
  - Dynamic props: interpolated from snapshots

Client B's perspective:
  - Client B's character: locally predicted, server-reconciled
  - Client A's character: interpolated from snapshots (100 ms behind)
  - Dynamic props: interpolated from snapshots

Server:
  - Runs physics for ALL entities (Client A character, Client B character, all props)
  - Reconciles Client A via snapshot corrections
  - Reconciles Client B via snapshot corrections
  - Both clients independently correct on divergence
```

This means each client independently predicts only its own character, while all other entities are interpolated. There is no cross-client prediction. Physics state converges on the server's authoritative result for both clients; the only coupling between clients is that their corrections both reference the same authoritative PhysicsWorld on the server.

**Interaction between characters:** If Client A's character collides with Client B's character, the collision is resolved on the server. Client A sees a correction applied to its position (if affected); Client B similarly. Both clients see each other's characters via interpolation, so the visual result of the collision will appear slightly delayed (~100 ms) on each client. This is standard behaviour for server-authoritative FPS replication.

---

## 10. Interest Management / Relevance

On large maps it is wasteful (and bandwidth-prohibitive) to replicate all entities to all clients. The **relevance system** culls entity updates to only the clients who can perceive them.

### Relevance Determination

Each entity's `NetworkIdentity.relevanceRadius` defines a sphere around the entity. A client is **relevant** to an entity if:

```
distance(entity.position, clientPlayer.position) <= entity.relevanceRadius
```

The check is done on the server each snapshot cycle (every 3 ticks). For performance, the relevance check is done on a spatial grid (cell size = max relevance radius), not a full O(entities × clients) loop.

Special cases:
- `relevanceRadius == 0.0f`: entity is **always relevant** to all clients. Used for game-mode managers, round timers, score entities, global audio triggers.
- Entities without a position (abstract game-mode entities): always relevant.

### Relevance State Transitions

```
Entity enters client's relevance set:
  → Server sends NetSpawn (reliable) for that entity to that client
  → Entity is added to per-client snapshot inclusion set

Entity leaves client's relevance set:
  → Server sends NetDestroy (reliable) to that client
  → Entity is removed from per-client snapshot inclusion set
  → Client destroys its local copy of the entity
```

This means clients may have different subsets of the world loaded at any given time. This is expected and correct. A sniper on a tower will have fewer entities relevant than a player in a dense urban area.

### Per-Client Relevance Sets

The server maintains `PerClientState::relevanceSet` (a `std::unordered_set<uint32_t>` of NetIds currently relevant to that client). This set is updated each snapshot cycle before snapshot building. The delta (entered/left) drives `NetSpawn`/`NetDestroy` messages.

---

## 11. Bandwidth Budget and Priorities

### Target Budget

The engine targets **< 64 KB/s outbound per client** from the server. At 20 snapshots/second this allows ~3200 bytes per snapshot packet. This is a soft cap — brief bursts (e.g. late-join full snapshot) are acceptable, but sustained overrun degrades quality for all clients sharing the server's uplink.

### Priority System

Each entity in the server's per-client relevance set has a computed **send priority** used to order snapshot packing:

```
priority = BASE_PRIORITY
           + (1.0 / distanceToClient) * DISTANCE_WEIGHT       // Closer = higher priority
           + (ticksSinceLastSent / UPDATE_INTERVAL) * STALE_WEIGHT  // Longer unsent = higher priority
           + (entity.tag == ARCHETYPE_TAG_CHARACTER ? PLAYER_BONUS : 0)
```

Constants (tunable in `replication_config.h`):
- `DISTANCE_WEIGHT = 1000.0f`
- `STALE_WEIGHT = 5.0f`
- `PLAYER_BONUS = 500.0f`

Player characters always receive a priority bonus to ensure their state is sent at full 20 Hz regardless of distance (within relevance radius).

### Snapshot Packing and Budget Enforcement

```
BuildSnapshot(client):
  entities = SortByPriority(client.relevanceSet)
  bytesUsed = SNAPSHOT_HEADER_SIZE  // 15 bytes

  for entity in entities:
      record = SerializeEntityRecord(entity, client.lastAckedSnapshotTick)
      if record is empty (no dirty components): skip

      if bytesUsed + record.size > SNAPSHOT_BUDGET_BYTES (3200):
          // Budget exceeded; defer entity to next snapshot
          // Its STALE_WEIGHT will increase priority next cycle
          entity.ticksSinceLastSent++
          continue

      append record to snapshot
      bytesUsed += record.size
      entity.ticksSinceLastSent = 0
```

Entities deferred due to budget accumulate stale weight, ensuring they are sent in the next available snapshot. No entity is indefinitely starved.

### Component-Level Size Estimates

| Component | Serialized Size |
|-----------|----------------|
| Entity record header (NetId + dirty mask) | 5 bytes |
| Transform (position + rotation, quantized) | 14 bytes |
| RigidBody (linear vel + angular vel, quantized) | 12 bytes |
| CharacterControllerState (full) | 32 bytes |
| Health | 5 bytes |
| WeaponState | 6 bytes |
| AnimationState (stub) | 4 bytes |

A typical player character record (Transform + RigidBody dirty) costs ~31 bytes. A 64-player server with all players in one area would require ~1984 bytes per snapshot just for player transforms — within budget.

---

## 12. Wire Format Reference

All values little-endian. Floats IEEE 754 single-precision unless noted.

### Quantization

Positions are transmitted as float32 (4 bytes each). Velocities are quantized to **int16** (2 bytes each) with a scale factor of 100 (range ±327.67 m/s — sufficient for any in-engine velocity). Rotations are transmitted as a **compressed quaternion**: the largest component is omitted and reconstructed from the constraint `|q| = 1`; the three transmitted components are int16 with scale 32767 (range [-1, 1]). This reduces rotation from 16 bytes (4× float32) to 7 bytes (3× int16 + 1 byte component index).

### Snapshot Header (15 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | `packetType` (0x02) |
| 1 | 4 | `snapshotTick` (uint32) |
| 5 | 8 | `serverTimestampUs` (uint64) |
| 13 | 2 | `entityCount` (uint16) |
| 15 | 4 | `baselineTick` (uint32) |

Total header: **19 bytes**.

### Entity Record Header (5 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | `netId` (uint32) |
| 4 | 1 | `dirtyComponentMask` (uint8) |

### Transform Payload (14 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | `posX` (float32) |
| 4 | 4 | `posY` (float32) |
| 8 | 4 | `posZ` (float32) |
| 12 | 1 | `largestComponent` index (uint8) |
| 13 | 6 | 3× quantized quaternion components (3× int16) |

Total: **19 bytes** (position 12 + rotation 7).

### RigidBody Payload (12 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 2 | `linVelX` (int16, ×100 m/s) |
| 2 | 2 | `linVelY` (int16, ×100 m/s) |
| 4 | 2 | `linVelZ` (int16, ×100 m/s) |
| 6 | 2 | `angVelX` (int16, ×100 rad/s) |
| 8 | 2 | `angVelY` (int16, ×100 rad/s) |
| 10 | 2 | `angVelZ` (int16, ×100 rad/s) |

Total: **12 bytes**.

### Health Payload (5 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 2 | `currentHp` (uint16) |
| 2 | 2 | `maxHp` (uint16) |
| 4 | 1 | `shieldPercent` (uint8, 0–100) |

### WeaponState Payload (6 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | `equippedWeaponId` (uint8) |
| 1 | 2 | `ammoInMag` (uint16) |
| 3 | 2 | `ammoReserve` (uint16) |
| 4 | 1 | `flags` (uint8: bits 0–1 fire mode, bit 2 reloading, bit 3 ADS) |

---

## 13. Implementation Notes and Gotchas

### Clock Synchronization

Client-side prediction requires that the client's tick counter be aligned to the server's tick counter. The engine implements a simple **NTP-style clock sync** at connection time (Section 5 of `task-08-transport.md`). The client's tick is offset so that input frames arrive at the server approximately one RTT ahead of the server's current tick — this gives the server time to process the input before the corresponding snapshot is due, without starving the server of inputs.

If clock sync drifts (measured by tracking whether input frames arrive before or after their target tick), the client adjusts its tick offset by ±1 tick per second to converge without visible jumps.

### Input Frame Buffering on the Server

The server queues incoming `InputFrame` packets per client in a small ring buffer. If multiple frames arrive for the same tick (duplicate delivery from the reliability layer), only the first is kept. If a frame is missing when the server needs it (tick arrives, no input received), the server **repeats the last known input** up to `MAX_INPUT_REPEAT_TICKS = 4` ticks before zeroing movement input. This prevents the server from freezing a player's character on brief packet loss.

### Snapshot Ordering Guarantee

The UDP transport does not guarantee order. The client may receive snapshot N+1 before snapshot N. Clients must check `snapshotTick` and discard any snapshot older than the last applied snapshot (`if incoming.snapshotTick <= lastAppliedTick: discard`). Out-of-order snapshots are not requeued — the next in-order snapshot will catch up via delta compression or full state.

### Avoiding NetId Exhaustion

With a `uint32_t` counter starting at 1 and incrementing per spawn, a session would need to spawn ~4.29 billion entities before exhausting the space. At 100 spawns/second this is ~496 days of continuous play. NetId exhaustion is not a practical concern and is documented here only for completeness.

### Thread Safety

On the server, snapshot building runs on the **main game thread** after `PhysicsWorld::Step()` completes. Network send/receive runs on a **network thread**. The snapshot is serialized into a `PacketBuffer` on the game thread and then handed off to the network thread via a lock-free queue. Clients receive packets on a network thread and push deserialized data into a `SnapshotInbox` consumed by the main render/simulation thread. Component writes from snapshot application are deferred via `EntityCommandBuffer` to avoid mid-frame mutation (see `task-03-ecs.md`).

### Version Compatibility

Replicated component formats and RPC IDs are versioned by a `uint32_t protocolVersion` exchanged during the handshake. If server and client versions differ, the connection is rejected with `DISCONNECT_REASON_VERSION_MISMATCH`. There is no backward-compatibility layer in Phase 6; all builds in a session must match.

---

*End of task-09-replication.md*
*Cross-reference: `task-07-gameplay-loop.md` (Project Lead), `task-03-ecs.md` through `task-06-physics.md` (Physics+Scene Lead)*
