# Appendix A — Gameplay Systems: Camera Replication, Damage, and Supporting Mechanics

**Module:** `engine::networking`, `engine::core::ecs`, `engine::gameplay`
**Phase:** 6 (Appendix)
**Author:** Networking Lead + Project Lead
**Cross-references:**
- `task-09-replication.md` — NetworkIdentity, snapshot model, RPC system, InputFrame wire format
- `task-02-input-system.md` — InputAction, InputBinding, InputReceiver
- `task-07-gameplay-loop.md` — 64-tick server loop, WorldState snapshot cadence
- `networking-lag-compensation.md` (Phase 2) — rewind window, HitscanValidationRequest, server history buffer

---

## Table of Contents

1. [Camera Orientation Replication](#1-camera-orientation-replication)
   - 1.1 Client A: Input to InputFrame
   - 1.2 Server: Apply and Store
   - 1.3 Server to Client B: Snapshot Path
   - 1.4 Client B: Receive and Render
   - 1.5 Wire Cost Summary
2. [Damage System](#2-damage-system)
   - 2.1 Authority Model
   - 2.2 Health Component
   - 2.3 Fire Event Flow (Client to Server)
   - 2.4 Server Hit Validation
   - 2.5 Damage Application via CommandBuffer
   - 2.6 EventBus: Server-Local Consequences
   - 2.7 Health Replication via Snapshot
   - 2.8 Death: Reliable RPC
   - 2.9 Client-Side Feedback
3. [Key Design Tensions and Resolutions](#3-key-design-tensions-and-resolutions)
   - 3.1 Lag Compensation
   - 3.2 Reliable Fire Events vs. Unreliable Health State
   - 3.3 CommandBuffer Ordering for Concurrent Damage

---

## 1. Camera Orientation Replication

Camera orientation — where a player is aiming — must be visible to the server (for hit validation) and to other clients (for visual representation of head/body direction). It is not replicated as a standalone component; it flows through two existing mechanisms depending on its purpose.

### 1.1 Client A: Input to InputFrame

Every tick (64 Hz) the `Input` system accumulates mouse delta from Win32 RawInput into the player's `yaw` and `pitch` accumulators. These are written into the `InputFrame` constructed that tick:

```
InputFrame (excerpt — full definition in task-09-replication.md §5):
  float  yaw    // Camera yaw in radians; full 2π range; wraps
  float  pitch  // Camera pitch in radians; clamped to ±89° on client before send
```

The `InputFrame` is sent to the server **unreliably at 64 Hz** with up to 3 frames of redundancy (Section 5 of task-09). Orientation is absolute (not a delta) so a dropped packet simply means the server uses the last received value rather than accumulating an error.

### 1.2 Server: Apply and Store

On receiving an `InputFrame`, the server:

1. Applies `yaw` and `pitch` to the authoritative `CharacterControllerState` for that player entity.
2. The resulting orientation is stored in the entity's `Transform` (rotation as `Quat`) in the ECS World.
3. The entity's `CharacterControllerState.yaw` field (sent with `RCB_TRANSFORM`/`RCB_RIGID_BODY` bits) is updated accordingly.
4. The server-side position history buffer (see `networking-lag-compensation.md §3.3`) stores the `Quat` alongside position for each tick, enabling rewind during hitscan validation.

The server does not validate pitch against ±89° — that clamp is the client's responsibility. The server does validate yaw continuity for abuse prevention (see `networking-lag-compensation.md §3.6`).

### 1.3 Server to Client B: Snapshot Path

`CharacterControllerState` is serialized as part of the `RCB_TRANSFORM` + `RCB_RIGID_BODY` bits for player entities. The `yaw` field is included:

```
CharacterControllerState (from task-09-replication.md §9):
  Vec3    position      // 12 bytes
  Vec3    velocity      // 12 bytes
  float   yaw           // 4 bytes  ← camera yaw replicated here
  bool    onGround      // 1 byte
  uint8_t moveState     // 1 byte
  uint8_t _pad[2]
```

`pitch` is **not** included in `CharacterControllerState` by default. It is only needed server-side (for hitscan validation) and client-side for the local player (reconstructed from the local camera). For remote players, pitch is typically expressed visually through an animation blend tree driven by `moveState`, not by direct replication of the raw pitch float. If explicit remote pitch is required (e.g. a leaning or prone system), add a `float pitch` field to `CharacterControllerState` and increase the payload by 4 bytes.

Client B receives the snapshot, resolves the NetId to its local entity handle via `NetworkRegistry::Resolve`, and writes the updated `CharacterControllerState` into the ECS via `EntityCommandBuffer` (deferred to end-of-frame).

### 1.4 Client B: Receive and Render

Client B renders player A's character through the interpolation system (task-09-replication.md §6). The interpolated `yaw` drives the character's visual body rotation. The rendering system reads `CharacterControllerState.yaw` from the interpolated sample and applies it to the character mesh's spine/head bone, or to a full-body rotation depending on the animation rig.

Because Client B's view of Client A is ~100 ms behind (interpolation delay), the displayed aim direction is that same 100 ms stale. This is expected and is the same latency that applies to all remote entity state.

### 1.5 Wire Cost Summary

| Path | Mechanism | Rate | Per-packet cost |
|------|-----------|------|----------------|
| Client A → Server (yaw + pitch) | `InputFrame` (unreliable) | 64 Hz | 8 bytes (2× float32) within the ~32-byte InputFrame |
| Server → Client B (yaw) | Snapshot, `RCB_TRANSFORM` bit | 20 Hz | 4 bytes within the 32-byte `CharacterControllerState` |

---

## 2. Damage System

### 2.1 Authority Model

The server is the sole authority for all damage decisions. Clients never declare that damage has occurred; they only:

- Send input indicating a fire action (`InputFrame.firePressed = true`).
- Send a `HitscanValidationRequest` (reliable) containing ray origin, direction, target entity, and `fireSerial`.
- Display optimistic local feedback (muzzle flash, crosshair reticle change) that the server may silently not confirm.

This prevents clients from fabricating damage and aligns with the server-authoritative model described in task-09-replication.md §1.

### 2.2 Health Component

`Health` is an ECS component registered with `kComponentId` in the component table and flagged with `RCB_HEALTH` in `ReplicatedComponentBit`:

```cpp
namespace engine::gameplay {

struct Health {
    static constexpr ComponentTypeId kComponentId = /* assigned at registration */;

    uint16_t currentHp;     // [0, maxHp]
    uint16_t maxHp;
    uint8_t  shieldPercent; // [0, 100]; absorbs damage before HP
};

} // namespace engine::gameplay
```

Wire layout matches task-09-replication.md §12 (5 bytes). Entities that can receive damage — players, destructible props, vehicles — carry this component. Entities without it are indestructible by definition; no special flag needed.

`Health` replicates **on change only** (dirty-mask based, as per task-09-replication.md §3). A full-health entity that never takes damage never appears in any snapshot's Health payload.

### 2.3 Fire Event Flow (Client to Server)

When the player fires:

1. `InputFrame.firePressed = true` and `InputFrame.fireSerial` (monotonically incrementing `uint32_t`) are set for that tick.
2. The `InputFrame` is sent unreliably at 64 Hz (with redundancy) as normal.
3. Simultaneously, a `HitscanValidationRequest` is sent via the **reliable channel**:

```
HitscanValidationRequest (defined in networking-lag-compensation.md §3.4):
  uint32_t  fireSerial     // Dedup key; matches InputFrame.fireSerial
  uint32_t  clientTick     // Tick at which client fired
  Vec3      origin         // Ray origin, world space (float32 × 3)
  Vec3      direction      // Normalized ray direction (quantized 3 × int16)
  Entity    targetEntity   // Client's claimed hit target (NetId)
```

The reliable delivery of `HitscanValidationRequest` ensures the server always processes the hit claim even under packet loss. The `InputFrame.firePressed` flag is an independent signal that triggers server-side effects (animation, sound RPC dispatch) even before hit validation completes.

### 2.4 Server Hit Validation

The server processes `HitscanValidationRequest` according to the procedure in `networking-lag-compensation.md §3.5`:

1. Dedup check via `fireSerial`.
2. Bounds check: `clientTick` must be within `[currentServerTick - kMaxRewindTicks, currentServerTick]`.
3. Rewind: restore `targetEntity` position/rotation from the history ring buffer at `clientTick`.
4. Ray cast: intersect `(origin, direction)` against the target's collider (sphere or AABB from the entity's `Renderable`).
5. Abuse check: ray direction cone ±5°, origin within 2.0 m of shooter's authoritative position at `clientTick`.

Hit confirmed → post `HitscanHitEvent{targetEntity, attackerEntity, fireSerial, weaponType}` on the server-side `EventBus`.  
Hit rejected → no event. No rejection message is sent to the client in Phase 6 (absence of confirmation is the signal).

### 2.5 Damage Application via CommandBuffer

A server-side system subscribes to `HitscanHitEvent` on the `EventBus`. On receipt it **does not mutate the ECS directly** — it pushes a deferred command through `CommandBuffer`:

```
CommandBuffer deferred writes (per HitscanHitEvent):
  1. Read target entity's Health component (current value).
  2. Compute newHp = max(0, currentHp - damageAmount).
  3. Push CmdType::Set<Health>{target, Health{newHp, maxHp, shieldPercent}} into CommandBuffer.
```

`CommandBuffer::flush()` applies all writes at a defined safe point in the frame order (after game logic, before snapshot build — see task-07-gameplay-loop.md). This guarantees:

- No mid-frame mutation while a `View` is iterating over `Health` components.
- Multiple damage events in one frame (shotgun pellets, area damage) all queue cleanly and are applied in the order `flush()` processes them.

### 2.6 EventBus: Server-Local Consequences

After `CommandBuffer::flush()`, the updated `Health` value is readable. A second pass checks for death:

```
if entity.health.currentHp == 0:
    EventBus::publish(EntityDiedEvent{deadEntity, killerEntity, weaponType})
```

Systems subscribing to `EntityDiedEvent` on the server:
- **RespawnSystem**: schedules a respawn timer for the dead entity.
- **ScoreSystem**: credits a kill to `killerEntity`.
- **GameModeSystem**: checks win conditions (last player standing, score limit, etc.).

`EventBus` is in-process only (header-only, no networking). These consequences execute entirely on the server within the same frame.

### 2.7 Health Replication via Snapshot

After `CommandBuffer::flush()` marks `Health` dirty, the snapshot builder (running at 20 Hz, every 3 ticks) includes the `RCB_HEALTH` bit for the affected entity. The 5-byte `Health` payload is written into the entity's snapshot record and delivered to all clients for whom the entity is relevant (task-09-replication.md §10).

Health is **state**, not an event. If the snapshot carrying the updated HP is lost, the next snapshot supersedes it. No special handling is needed for Health delivery beyond the normal snapshot ACK/delta machinery.

### 2.8 Death: Reliable RPC

Death is a discrete, non-supersedable event. If the "you died" signal is lost and only the HP=0 snapshot arrives, clients may miss triggering the death animation, respawn UI, kill feed entry, and audio. A dedicated reliable RPC is sent alongside the snapshot:

```cpp
ENGINE_RPC(PlayerDied, Reliable, AllClients,
    uint32_t deadEntityNetId,
    uint32_t killerEntityNetId,
    uint8_t  weaponType,
    Vec3     deathPosition
)
```

Delivery guarantees:
- `PlayerDied` is reliable (retransmitted until ACKed by each client).
- The RPC targets `AllClients` so all connected clients update their kill feed simultaneously.
- The dead player's own client also receives it (triggers respawn UI, death camera).

The `HP=0` snapshot and the `PlayerDied` RPC may arrive in either order. Client-side systems must handle both orderings gracefully: the first to arrive triggers the death state; the second is a no-op if death state is already active.

### 2.9 Client-Side Feedback

On the local client (attacker):
- Optimistic hit feedback (crosshair flash, hit marker) fires immediately on `InputFrame.firePressed`, driven by the client's own ray cast result. This may be wrong (server rejects the hit) — the absence of a confirmed `HitscanHitEvent` RPC is the correction signal, but Phase 6 does not send explicit rejections.
- If a `PlayerDied` RPC arrives for the killed entity, the kill notification is shown.

On Client B (victim):
- `Health` update arrives via snapshot: health bar decrements.
- `PlayerDied` RPC arrives: death animation triggers, respawn timer starts, death camera activates.

On all other clients:
- `PlayerDied` RPC: kill feed entry added.
- Snapshot: health bar for the dead entity's proxy (if visible in HUD) goes to zero.

---

## 3. Key Design Tensions and Resolutions

### 3.1 Lag Compensation

**The problem:** A client fires at tick T. Due to RTT, the `HitscanValidationRequest` arrives at the server at tick T + (RTT/2 in ticks). The server must validate the hit against the world state as the client *saw* it at tick T — not the current world state. Without rewinding, fast-moving targets appear to have ghost hitboxes and shots that visually connect are rejected.

**Resolution:** This is fully specified in `networking-lag-compensation.md §3`. The server maintains a ring buffer of `EntityHistory{position, rotation, tick}` for all replicated entities for the last `kRewindWindowTicks = 6` ticks (200 ms at 30 Hz). On receiving `HitscanValidationRequest`, the server rewinds `targetEntity` to `clientTick` before ray casting.

**Integration point with the damage system:** The `HitscanHitEvent` posted in §2.4 above is the output of the lag-compensated validation procedure. No changes to the damage system are required; the lag compensation layer is transparent beneath it. Implementors should read `networking-lag-compensation.md` in full before implementing `HitscanValidationRequest` handling.

**Phase 6 status:** Lag compensation is documented as a Phase 7 item in task-09-replication.md §6. For Phase 6 initial implementation, hit validation is performed against the **current server world state** (no rewind). The `clientTick` field is present in `HitscanValidationRequest` from the start so the wire format does not need to change when lag compensation is added in Phase 7.

---

### 3.2 Reliable Fire Events vs. Unreliable Health State

**The problem:** The current transport offers one delivery mode per channel:
- `ReliableChannel`: retransmits until ACKed. Correct for fire events and death RPCs. Problematic for health updates: a dropped health snapshot causes a retransmit stall, adding latency to a value that would have been superseded by the next snapshot anyway.
- The existing snapshot path (unreliable UDP) is correct for health state but has no ordering guarantee.

Mixing both needs through `ReliableChannel` alone means health updates could be delayed by retransmit backpressure from unrelated reliable messages.

**Resolution: Unreliable-Ordered Channel**

Add a third delivery mode alongside `ReliableChannel`:

| Channel | Delivery | Ordering | Use cases |
|---------|----------|----------|-----------|
| `ReliableChannel` (existing) | Guaranteed delivery | Ordered within channel | Spawn/destroy, death RPC, fire events, score updates |
| Snapshot UDP (existing) | Best-effort | Unordered | Transform, physics state |
| **`UnreliableOrderedChannel`** *(new)* | Best-effort | Newest-wins per entity | Health, weapon state, animation state, camera orientation supplemental |

`UnreliableOrderedChannel` tracks the highest sequence number received per (NetId, componentBit) pair. Packets with a sequence number ≤ the last applied value are silently dropped. This gives health updates the same "newest state wins" semantics as the snapshot, with independent flow control from `ReliableChannel`.

**Implementation sketch:**

The channel sits alongside `ReliableChannel` inside `Session`. It requires:
1. A per-(NetId, componentBit) `lastAppliedSeq: uint16_t` table on the client.
2. A sequence counter per entity per component on the server, incremented on each send.
3. No ACK machinery — dropped packets are not retransmitted.

Wire overhead per component update: 2 bytes (sequence) + existing component payload. At 20 Hz for 10 active players with Health updates: ~400 bytes/s per client — negligible.

**Phase 6 guidance:** Implement `UnreliableOrderedChannel` before wiring up `Health`, `WeaponState`, and `AnimationState` replication. `ReliableChannel` should remain exclusively for events where loss would produce a persistent gameplay state error.

---

### 3.3 CommandBuffer Ordering for Concurrent Damage

**The problem:** Multiple damage sources can fire in the same tick — a shotgun with 8 pellets each producing a `HitscanHitEvent`, or two players shooting the same target simultaneously. `CommandBuffer` queues writes and applies them in `flush()` order. The final HP depends on the order the commands are processed, and naive additive writes can interact incorrectly:

```
Scenario: entity has 50 HP. Two simultaneous hits: 30 damage and 40 damage.
  Command 1 reads HP=50, pushes Set{hp=20}
  Command 2 reads HP=50, pushes Set{hp=10}
  flush() applies Set{hp=20} then Set{hp=10} → final HP = 10  (correct by coincidence)
  OR
  flush() applies Set{hp=10} then Set{hp=20} → final HP = 20  (wrong — 30-damage hit "wins")
```

Absolute `Set` writes are order-dependent and silently wrong when concurrent.

**Resolution: Damage Accumulation Pattern**

Replace `CmdType::Set<Health>` with `CmdType::Delta<Health>`:

```
Each HitscanHitEvent pushes:
  CmdType::Delta<Health>{target, -damageAmount}

flush() for CmdType::Delta<Health>:
  currentHp = max(0, entity.health.currentHp + delta)
  entity.health.currentHp = currentHp
```

`Delta` commands commute: applying `-30` then `-40` or `-40` then `-30` both yield the same result. The entity cannot go below 0 because `max(0, ...)` is applied at each delta, or once after summing all deltas for the frame (either is correct; summing first then clamping is one fewer branch).

**Additional consideration — shield absorption order:**

If the entity has a shield (`Health.shieldPercent > 0`), damage should deplete the shield before HP. With accumulated deltas this must be resolved as a single transaction at flush time:

```
flush() for accumulated Delta<Health> on entity E:
  totalDamage = sum of all negative deltas for E this frame
  if shieldHp > 0:
      shieldAbsorbed = min(totalDamage, shieldHp)
      totalDamage   -= shieldAbsorbed
      shieldHp      -= shieldAbsorbed
  currentHp = max(0, currentHp - totalDamage)
```

This ensures shield is correctly consumed regardless of how many damage sources fired.

**Phase 6 guidance:** Implement `CmdType::Delta<T>` alongside `CmdType::Set<T>` in `CommandBuffer`. Only `Health` requires Delta semantics in Phase 6; all other components continue using `Set`. Document the invariant: **any component that can receive concurrent writes from independent sources must use `Delta`, not `Set`**.

---

*End of appendix-A-gameplay-systems.md*
*Cross-references: `task-09-replication.md`, `task-02-input-system.md`, `task-07-gameplay-loop.md`, `networking-lag-compensation.md`*
