# Networking: Lag Compensation and Client-Side Prediction

Status: Approved (Phase 2)
Owner: Networking Lead
Task: #12
References: networking-architecture.md, ecs-design.md §8, scope-networking.md

---

## 1. Client-Side Prediction

### 1.1 Policy

**The client predicts only entities it owns** (i.e., `NetOwner::clientSlot` matches the client's assigned slot). All other entities are interpolated from server snapshots.

The client does NOT predict physics or ECS systems that are server-authoritative (spawning, game logic). Prediction is limited to player movement — reading local input and integrating the player's `Transform` locally before the server's authoritative state arrives.

### 1.2 Prediction loop

Each `FixedUpdate` on the client:
1. Advance `predictedTick` by 1.
2. Apply input for `predictedTick` to the locally-owned entity via the same movement system the server uses.
3. Store the predicted state `(predictedTick, predictedTransform)` in a circular buffer (ring buffer from `core::containers`).
4. Send the input message for `predictedTick` to the server.

Prediction buffer depth: `kPredictionBufferDepth = 64` ticks (≈ 1.0 s at 64 Hz). Inputs older than this are dropped.

### 1.3 Prediction input message

Sent from client to server in `Update` phase (at render rate, not 64 Hz — input is sampled every rendered frame for lower latency):

```cpp
struct InputMessage {
    uint32_t tick;           // which server tick this input is for
    float    moveX;          // [-1, 1] quantized to 8 bits signed
    float    moveZ;          // [-1, 1] quantized to 8 bits signed
    float    yawDelta;       // degrees, quantized to 12 bits signed, range ±720°
    float    pitchDelta;     // degrees, quantized to 10 bits signed, range ±180°
    bool     jump;
    bool     fire;           // triggers hitscan validation (see §3)
    uint32_t fireSerial;     // monotonic counter per fire event, for dedup
};
```

Wire encoding (BitWriter):
- `tick`: varint
- `moveX`, `moveZ`: 8-bit signed (1 sign + 7 magnitude)
- `yawDelta`: 12 bits (1 sign + 11 magnitude), range ±720°/2048 ≈ 0.35°/step
- `pitchDelta`: 10 bits signed
- `jump`, `fire`: 1 bit each
- `fireSerial`: varint (only present if `fire == true`)

Total without fire: ~5 bytes. With fire: ~7 bytes. Input messages are sent unreliably at 60 Hz but with redundancy: each packet includes the current and up to 3 previous inputs (redundant send pattern), so a single packet loss does not drop an input.

---

## 2. Server Reconciliation

### 2.1 Server receives input

On receiving an `InputMessage` for tick T:
- If T is in the past (T < current server tick) and within the rewind window (`currentTick - T <= kRewindWindowTicks`): store the input in the client's input history and apply it during the rewind for hit validation.
- If T == current server tick: apply to the simulation immediately.
- If T is in the future (T > currentTick): buffer in the client's future-input queue (up to `kMaxAheadTicks = 4`).

Duplicate inputs (same `tick` and `fireSerial`) are silently discarded.

### 2.2 Server reconciliation message

After each simulation tick, the server sends a reconciliation message to each client for their owned entity:

```cpp
struct ReconcileMessage {
    uint32_t tick;           // authoritative tick
    Vec3     position;       // authoritative position (quantized)
    Quat     rotation;       // authoritative rotation (smallest-three)
};
```

Sent as a **reliable** message (flag `kFlagReliable`). Since it's reliable, the client is guaranteed to eventually receive it; the network ordering guarantee is NOT required (older reconcile messages that arrive after newer ones are discarded by tick comparison).

### 2.3 Client applies reconciliation

On receiving a `ReconcileMessage` for tick T:
1. Look up the predicted state at tick T from the circular buffer.
2. Compute divergence: `distance(authoritative.position, predicted.position)`.
3. If divergence < `kReconcileThreshold = 0.05 m`: no correction needed (accept prediction).
4. If divergence ≥ `kReconcileThreshold`:
   a. Write the authoritative state into the ECS for the owned entity.
   b. Re-simulate from tick T+1 to `predictedTick` using the stored inputs from the prediction buffer.
   c. Update the prediction buffer with corrected states.

Re-simulation uses the same movement system function the server uses — both must call the same code path (enforced by both registering the same free function pointer via `world.addSystem`).

---

## 3. Lag Compensation (Hitscan Validation)

### 3.1 Purpose

When a client fires a hitscan weapon (a ray cast for hit detection), the client was seeing the world at some time in the past due to latency. The server must validate the hit against the world state as it appeared to the client at fire time — not the current world state.

### 3.2 Rewind window

```cpp
constexpr uint32_t kRewindWindowTicks  = 13;   // at 64 Hz ≈ 200 ms
constexpr uint32_t kMaxRewindTicks     = 13;   // hard cap; cannot rewind more than this
```

`kRewindWindowTicks = 13` is the default maximum rewind. It is configurable via `[network].rewindWindowTicks` but cannot exceed `kMaxRewindTicks` (a hard cap to prevent abuse). Typical use: set to the client's measured round-trip latency in ticks, clamped to the window.

At 64 Hz, 13 ticks ≈ 203 ms. This matches the scope-networking.md requirement of ≤ 200 ms rewind window.

### 3.3 Server history buffer

The server maintains a **position history** for all replicated entities for the last `kRewindWindowTicks + 2` ticks. Stored as a ring buffer:

```cpp
struct EntityHistory {
    Vec3     position;
    Quat     rotation;
    uint32_t tick;
};
// Per entity: ring buffer of kRewindWindowTicks + 2 entries
```

This is a lightweight copy of `Transform` components only; full ECS state is not snapshotted. Allocated from the world arena at entity spawn.

### 3.4 Hit validation message

```cpp
struct HitscanValidationRequest {
    uint32_t    fireSerial;      // matches InputMessage::fireSerial; used for dedup
    uint32_t    clientTick;      // the tick at which the client fired
    Vec3        origin;          // ray origin (quantized)
    Vec3        direction;       // normalized ray direction (quantized, 3 × 16-bit signed)
    Entity      targetEntity;    // entity the client claims was hit
};
```

Sent as a **reliable** message from client to server.

### 3.5 Server validation procedure

On receiving `HitscanValidationRequest`:

1. **Dedup check**: if `fireSerial` for this client has already been processed, discard.
2. **Sanity check**: `clientTick` must be within `[currentServerTick - kMaxRewindTicks, currentServerTick]`. If not, reject (log at `LOG_WARN`, no penalty).
3. **Rewind**: restore `targetEntity`'s position and rotation from the history buffer at `clientTick`.
4. **Ray cast**: intersect the ray `(origin, direction)` against the entity's collider (a simple sphere/AABB from the `Renderable`'s AABB in v1).
5. **Result**:
   - Hit confirmed: post a `HitscanHitEvent{targetEntity, fireSerial}` on the server-side event bus.
   - Hit rejected: no event; the attacker's client does not receive a confirmation.

The validation result is authoritative. Client-side hit feedback is immediate and optimistic; the server may silently disagree (no explicit rejection message in v1 — the absence of a hit event is the rejection).

### 3.6 Abuse prevention

- The ray `direction` must be within ±5° of the vector from the shooter's server-authoritative position to the claimed target entity's position. If outside this cone, the hit is rejected.
- `origin` must be within `kMaxPositionDivergence = 2.0 m` of the shooter's server-authoritative position at `clientTick`. If further, reject.
- No rewind beyond `kMaxRewindTicks`: hard cap, not configurable by clients.

---

## 4. Interpolation for Non-Owned Entities

Non-owned entities (opponents, NPCs) are smoothly interpolated between received snapshots on the client.

### 4.1 Interpolation buffer

Each non-owned replicated entity maintains an interpolation buffer: a ring of the last `kInterpolationBufferSize = 8` snapshot entries `(tick, Transform)`.

### 4.2 Render time vs. simulation time

The client renders entities at a deliberate **render time** = `currentServerTime - kInterpolationDelay`. The default `kInterpolationDelay = 2` ticks (≈ 31 ms at 64 Hz). This ensures there are usually two snapshots to interpolate between.

The delay is configurable: `[network].interpolationDelayTicks`. Higher delay = smoother interpolation at the cost of more visual latency for remote entities.

### 4.3 Interpolation logic

```
renderTick = serverEstimatedCurrentTick - kInterpolationDelay
Find the two snapshots surrounding renderTick: snapA (tick ≤ renderTick) and snapB (tick > renderTick).
t = (renderTick - snapA.tick) / (snapB.tick - snapA.tick)
position = lerp(snapA.position, snapB.position, t)
rotation = slerp(snapA.rotation, snapB.rotation, t)
```

If only one snapshot is available (buffer underflow): hold the last known position. If the buffer overflows (no new snapshots for > `kInterpolationBufferSize` ticks): teleport to the most recent known position and log at `LOG_WARN`.

`lerp` and `slerp` come from `core::math`.
