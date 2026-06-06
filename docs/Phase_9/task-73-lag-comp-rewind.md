# Task #73 — TD-03: Full Lag Compensation Rewind

**Phase 9 — physics / app — Version 0.9.x**
**Audience:** Physics developer, Networking Lead
**Severity:** 🔴 Critical debt — hitscan hit-registration is incorrect at >0 ms latency
**Depends on:** Phase 7 #52 (PhysicsWorld), Phase 7 #59 (DamageSystem, N2 partial rewind)

---

## 1. Goal

When a client fires a hitscan weapon, the server validates the shot against the world state at the client's firing tick — not the current server state. A target moving at 10 m/s with 100 ms RTT is ~1 m ahead of where the client saw them. Without rewind, every shot is off; with rewind, hitscan feels correct.

Phase 7 / N2 implemented a partial rewind: `DamageSystem::rewindToTick()` manually saves and restores `Transform` components by reading from `ReplicationSystem::historyRing_`. This works but is fragile — it bypasses `PhysicsWorld`'s internal body state, creating a window where the physics body positions and the ECS Transforms are inconsistent.

This task replaces the manual rewind with a clean `PhysicsWorld` API.

---

## 2. Current State

**`src/app/DamageSystem.cpp`** — `rewindToTick()`:
```cpp
// Manually saves Transform components, overwrites them from history ring,
// then calls physics_.setTransformByEntity() to update physics bodies.
// After raycast, restoreTransforms() copies them back.
```

Problems:
- `setTransformByEntity()` moves the physics body but doesn't update its velocity or broad-phase cell — subsequent raycasts that step physics will see stale state.
- If two requests arrive for the same tick with different targets, the "saved" transforms are only captured once — the second rewind on top of an already-rewound state is incorrect.
- There is no way to rewind the static BVH (it shouldn't change, but the current code doesn't assert this).

---

## 3. PhysicsWorld::rewindAndRaycast()

Add a single atomic operation that rewinds, raycasts, and restores:

**File:** `src/physics/public/physics/PhysicsWorld.h`

```cpp
// Rewind all networked entity positions to the snapshot nearest to `tick`,
// perform a raycast, then restore. Thread-safe only if called from the server
// game thread (which is single-threaded in Phase 9).
RaycastResult rewindAndRaycast(
    uint32_t                               tick,
    const core::math::Vec3&                origin,
    const core::math::Vec3&                direction,
    float                                  maxDist,
    const networking::ReplicationSystem&   replication,
    networking::NetworkRegistry&           registry);
```

**File:** `src/physics/PhysicsWorld.cpp` — implementation:

```cpp
RaycastResult PhysicsWorld::rewindAndRaycast(tick, origin, dir, maxDist,
                                              replication, registry)
{
    // 1. Save current body transforms.
    std::vector<SavedBody> saved;
    for (auto& [id, body] : bodies_) {
        saved.push_back({ id, body.position, body.rotation });
    }

    // 2. Find the snapshot at or before `tick` in the history ring.
    const auto* snap = replication.getSnapshotAtTick(tick);
    if (snap) {
        for (const auto& [netId, hist] : snap->transforms) {
            EntityId e = registry.find(netId);
            if (e == kInvalidEntity) continue;
            if (auto it = bodies_.find(e); it != bodies_.end()) {
                it->second.position = hist.position;
                it->second.rotation = hist.rotation;
                broadPhase_.updateDynamic(e, it->second.aabb());
            }
        }
    } else {
        LOG_WARN("PhysicsWorld::rewindAndRaycast: tick {} not in ring; "
                 "raycasting against live positions", tick);
    }

    // 3. Raycast.
    RaycastResult result = raycast(origin, dir, maxDist);

    // 4. Restore saved body transforms.
    for (const auto& s : saved) {
        auto it = bodies_.find(s.entityId);
        if (it == bodies_.end()) continue;
        it->second.position = s.position;
        it->second.rotation = s.rotation;
        broadPhase_.updateDynamic(s.entityId, it->second.aabb());
    }

    return result;
}
```

---

## 4. DamageSystem Update

Replace `rewindToTick()` / `restoreTransforms()` with the single API call:

```cpp
// Before:
std::vector<SavedTransform> rewindSaved;
const bool rewound = rewindToTick(req.clientTick, rewindSaved);
const auto hit = physics_.raycast(req.origin, dir, 1000.0f);
if (rewound) restoreTransforms(rewindSaved);

// After:
const auto hit = physics_.rewindAndRaycast(
    req.clientTick, req.origin, dir, 1000.0f, *replication_, registry_);
```

Remove `rewindToTick()`, `restoreTransforms()`, `SavedTransform` from `DamageSystem.h/.cpp`.

---

## 5. Snapshot Ring Access

`ReplicationSystem::historyRing_` is currently `private`. Add a `getSnapshotAtTick(uint32_t) const` accessor (already referenced in the N2 implementation — verify it is public or add the declaration).

The ring buffer stores 32 snapshots at ~20 Hz ≈ 1.6 seconds of history. At 100 ms RTT, the client tick is always within the ring. At > 1.6 s RTT (pathological), fall back to live positions with a `LOG_WARN` (current behaviour).

---

## 6. Files to Modify

| File | Change |
|------|--------|
| `src/physics/public/physics/PhysicsWorld.h` | Add `rewindAndRaycast()` declaration |
| `src/physics/PhysicsWorld.cpp` | Implement `rewindAndRaycast()` |
| `src/app/public/app/DamageSystem.h` | Remove `SavedTransform`, `rewindToTick()`, `restoreTransforms()` |
| `src/app/DamageSystem.cpp` | Replace manual rewind with `rewindAndRaycast()` |
| `src/networking/public/networking/ReplicationSystem.h` | Verify `getSnapshotAtTick()` is accessible |

---

## 7. Tests

**File:** `tests/physics/LagCompTests.cpp` (label: unit)

- Two bodies at positions A and B at tick 10; at tick 11, both move to C and D. Call `rewindAndRaycast(10, ...)` targeting A. Verify hit at A, not C.
- After `rewindAndRaycast()`, verify body positions are restored to C/D (not left at A/B).
- Tick not in ring: verify `rewindAndRaycast()` falls back to live positions and logs `LOG_WARN`.
- Concurrent calls on same tick (simulate two simultaneous hitscan requests): verify each gets the correct result and positions are fully restored after each.

---

## 8. Known Issues

- **Multi-pellet shotgun:** A shotgun fires N pellets in one tick, each as a separate `HitscanValidationRequest`. Each call to `rewindAndRaycast()` saves and restores independently. This is correct but O(N) saves+restores per tick — fine for Phase 9 (max 8 pellets). For Phase 10, consider a batch version that does one save/restore for multiple rays.
- **Static geometry:** The static BVH is not rewound (static geometry doesn't move). This is correct but should be asserted — if a "static" entity moves during PIE or via scripting, its BVH entry will be stale in rewind.
