# Phase 7 Technical Debt Register

**Recorded:** 2026-06-04
**Scope:** Tasks #49–#59 (Phase 7, IGame through DamageSystem)

Items are grouped by severity. Each entry names the affected file(s), describes the problem, and lists the earliest phase at which it should be resolved.

---

## Severity Legend

| Symbol | Meaning |
|--------|---------|
| 🔴 Critical | Affects correctness of gameplay or data integrity |
| 🟠 High | Feature is incomplete or will break under realistic load |
| 🟡 Medium | Developer experience pain; content authors blocked |
| 🟢 Low | Technical hygiene; no user-visible impact today |

---

## 🔴 Critical

### TD-01 — Hitscan validation trusts client-clamped pitch
**File:** `src/app/DamageSystem.h/.cpp`, `src/core/public/core/input/InputFrame.h`
**Problem:** `InputFrame.lookPitchDelta` is clamped ±89° on the client before the frame is sent (per task-50 spec). The server receives and trusts that clamped value without re-clamping. A malicious client that bypasses the SDK and sends an out-of-range pitch can fire through floors or ceilings. The server `DamageSystem` raycast follows whatever direction the frame carries.
**Fix:** Re-apply the ±89° pitch clamp server-side in `DamageSystem` before constructing the ray direction. One line.
**Resolution Phase:** Hotfix before Phase 9 networking.

---

### TD-02 — `fireSerial` dedup has no expiry window
**File:** `src/app/DamageSystem.cpp`
**Problem:** `DamageSystem` deduplicates `HitscanValidationRequests` by `fireSerial` to prevent double-damage. The dedup set grows forever — it is never pruned. Over a long match, memory grows without bound and old serials from disconnected clients are never reclaimed.
**Fix:** Pair each serial with the server tick at receipt; prune entries older than 10 seconds (640 ticks) during the dedup check.
**Resolution Phase:** Phase 9 (before any multiplayer stress testing).

---

### TD-03 — Lag compensation rewind is a placeholder
**File:** `src/app/DamageSystem.cpp`, `src/networking/public/networking/ReplicationSystem.h`
**Problem:** Task-59 explicitly ships without lag-comp rewind. The 32-snapshot ring buffer exists in `ReplicationSystem` but `DamageSystem` raycasts against the *current* world state, not the state at `clientTick`. At 100 ms RTT, a fast-moving target can be unhittable from the client's perspective while being trivially hittable server-side (or vice-versa).
**Fix:** Thread the `clientTick` field from `HitscanValidationRequest` through to a `PhysicsWorld::rewindAndRaycast()` call that interpolates entity positions from the snapshot ring buffer.
**Resolution Phase:** Phase 9 (requires snapshot rewind API addition to `PhysicsWorld`).

---

## 🟠 High

### TD-04 — Navmesh asset field is dead
**File:** `src/core/public/core/scene/SceneGlobals.h`
**Problem:** `SceneGlobals.navmeshAsset` (a path/hash) is defined and serialized but no baking tool populates it and no runtime system reads it. AI navigation is effectively non-functional even if a game mode tries to use it.
**Fix:** Either add a navmesh bake step to the asset_cooker pipeline or remove the field until Phase 9 introduces navigation.
**Resolution Phase:** Phase 9.

---

### TD-05 — `PhysicsWorld` BVH is static after activation
**File:** `src/physics/internal/BroadPhase.h/.cpp`
**Problem:** The static BVH is built once at `Scene::activate()`. Any entity whose collider changes shape or is destroyed after activation leaves a stale leaf in the BVH. Dynamic objects use the 4 m cell grid correctly, but static objects that are destroyed (e.g., destructible walls) are still tested against.
**Fix:** Add `BroadPhase::removeStatic(EntityId)` and call it from `Scene` when a static body is removed. Rebuilding the full BVH on every removal is acceptable for Phase 8 content volume (< 2000 statics).
**Resolution Phase:** Phase 8 or 9, triggered by destructible prop authoring.

---

### TD-06 — `PhysicsWorld` constraint solver is single-threaded
**File:** `src/physics/PhysicsWorld.cpp`
**Problem:** The narrow-phase and constraint solver run sequentially on the game thread. At the 64 Hz fixed tick with 64 players and ~200 dynamic objects, solver time will exceed the 15.6 ms budget on typical dev hardware. No profiling data yet, but the architecture has no parallelism hook.
**Fix:** Split broad-phase pair generation onto a task pool; run narrow-phase island detection in parallel. Not required for Phase 8 content but must be planned before shipping.
**Resolution Phase:** Phase 9 (requires task-scheduler addition to `engine::core`).

---

### TD-07 — `EntityFactory` archetypes are not serializable
**File:** `src/core/public/core/ecs/EntityFactory.h`
**Problem:** Archetypes are registered as C++ function pointers (`ArchetypeFn`). There is no way to serialize "this entity was spawned from archetype X with these override values" — the scene format stores raw component data, discarding archetype identity. This means round-tripping an entity through save/load loses the authoring intent and prefab overrides cannot be tracked.
**Fix:** Add an optional `archetypeName` string field to the entity table row in `SceneSerializer`. When present, the loader calls `EntityFactory::spawn()` for defaults then applies the stored component deltas on top. This is the prerequisite for the Phase 8 prefab system (Task #65).
**Resolution Phase:** Phase 8 (Task #65 depends on it).

---

### TD-08 — `GameModeStateBlob` 256-byte limit is unenforced
**File:** `src/app/public/app/IGameMode.h`
**Problem:** The spec defines `GameModeStateBlob` as max 256 bytes but there is no `static_assert` or runtime check. A `IGameMode` implementation that serializes more than 256 bytes silently corrupts adjacent memory in the snapshot packet.
**Fix:** Add `static_assert(sizeof(GameModeStateBlob) <= 256)` to the struct definition, or switch to a `std::array<uint8_t, 256>` with `ByteWriter` bounds checking.
**Resolution Phase:** Phase 8 (trivial fix, high risk if left).

---

## 🟡 Medium

### TD-09 — `ColliderShape` has no editor widget
**File:** `src/editor/panels/InspectorPanel.h/.cpp`, `src/physics/public/physics/ColliderShape.h`
**Problem:** `ComponentEditorRegistry` has no registered widget for `Collider`. Selecting an entity with a collider in the Inspector shows nothing for that component. There is also no viewport overlay to visualize collider extents.
**Fix:** Phase 8 Task #61 (Collision Geometry Editor) resolves this entirely.
**Resolution Phase:** Phase 8.

---

### TD-10 — `SceneSerializer` silently skips unknown components without logging
**File:** `src/tools/SceneSerializer.cpp`
**Problem:** Forward-compatible unknown-component skipping is correct behavior, but it is silent. If a component ID is missing from the registry because of a registration bug or version mismatch, the loaded entity will be silently missing components. There is no warning in the log.
**Fix:** Call `LOG_WARN` with the unknown `ComponentTypeId` and entity handle whenever a component is skipped. One line in the skip branch.
**Resolution Phase:** Phase 8 (include in Task #64 scene-load work).

---

### TD-11 — `PhysicsMaterialTable` has no editor UI
**File:** `config/physics_materials.toml`, `src/physics/public/physics/PhysicsMaterialTable.h`
**Problem:** Physics materials (friction, restitution, sound surface) are only editable by opening `physics_materials.toml` in a text editor. There is no hot-reload and no inspector panel. Content authors cannot iterate on feel without restarting the editor.
**Fix:** Add a `PhysicsMaterialsPanel` (a simple ImGui table) that reads/writes `physics_materials.toml` via `toml++` and calls `PhysicsMaterialTable::reload()`.
**Resolution Phase:** Phase 8 (small; include in Task #61 or as a standalone panel).

---

### TD-12 — Collision layer matrix has no editor UI
**File:** `src/physics/public/physics/QueryFilter.h`
**Problem:** The 16-layer collision matrix is defined in code and/or TOML. There is no visual layer matrix editor (the kind familiar from Unity/Unreal). Setting up new layers requires editing source or a config file and knowing the bit layout.
**Fix:** Add a `CollisionLayerPanel` (16×16 checkbox grid, layer name editing) to the editor, reading/writing a `collision_layers.toml`.
**Resolution Phase:** Phase 8 (include in Task #61).

---

### TD-13 — `ComponentEditorRegistry` requires manual widget registration
**File:** `src/editor/panels/InspectorPanel.h`
**Problem:** Every component requires a manually written ImGui widget and a manual call to `ComponentEditorRegistry::registerWidget<T>()`. There is no reflection or auto-discovery. As the component count grows this becomes a maintenance surface — it is easy to add a component and forget the widget.
**Fix (short-term):** Document the requirement in the Phase 8 component workflow doc (Task #62) and add an `ENGINE_ASSERT` in `InspectorPanel::draw()` that fires when a registered component type has no widget.
**Fix (long-term):** A compile-time type-list approach using a `ComponentTraits<T>` specialization that bundles widget, serializer, and default factory together. Phase 9 scope.
**Resolution Phase:** Short-term fix in Phase 8 (Task #62). Full reflection in Phase 9.

---

### TD-14 — `ENGINE_RPC` macro has no compile-time validation
**File:** `src/networking/public/networking/RPC.h`
**Problem:** The `ENGINE_RPC` macro generates RPC registration boilerplate. There is no `static_assert` that the handler signature matches the expected prototype. A signature mismatch causes a runtime crash or undefined behavior, not a compile error.
**Fix:** Replace the macro with a template function `registerRpc<Fn>()` that uses `std::is_invocable` to validate the handler signature at compile time.
**Resolution Phase:** Phase 9 (networking refactor).

---

### TD-15 — `SceneSerializer` asset refs computed without caching
**File:** `src/tools/SceneSerializer.cpp`
**Problem:** Asset references are stored as SHA-256 hex strings. SHA-256 is computed at serialize time on the raw asset bytes. For a scene with 500 mesh references this is measurably slow (several hundred ms on a debug build). There is no `.meta` sidecar or content-hash cache.
**Fix:** Produce a `.easset.meta` file during `asset_cooker` runs that caches the SHA-256 alongside mtime. `SceneSerializer` checks mtime; recomputes only on change.
**Resolution Phase:** Phase 8 (include in Task #60 import pipeline).

---

## 🟢 Low

### TD-16 — `PredictionBuffer` slot count is a magic number
**File:** `src/networking/public/networking/PredictionBuffer.h`
**Problem:** 128 slots (2 s at 64 Hz) is hardcoded. The comment documents the math but the constant is not named. If the game tick rate changes the slot count goes stale silently.
**Fix:** `static constexpr uint32_t kPredictionWindowTicks = kGameTickHz * 2;`
**Resolution Phase:** Phase 9.

---

### TD-17 — `SnapshotBuffer` extrapolation has no quality fallback
**File:** `src/networking/public/networking/SnapshotBuffer.h`
**Problem:** Linear extrapolation runs up to 200 ms. Beyond 200 ms the spec is silent. Under heavy packet loss (mobile, bad WiFi) entities either freeze or extrapolate indefinitely depending on the current implementation.
**Fix:** After the extrapolation cap, hold the last known position. Log `LOG_WARN` once per entity per second when extrapolation cap is hit, so QA can detect excessive packet loss in test runs.
**Resolution Phase:** Phase 9.

---

### TD-18 — PIEController in-process networking can conflict with editor's own session
**File:** `src/editor/PIEController.h/.cpp`
**Problem:** PIE spawns an in-process local server+client pair on a hardcoded port. If the editor is already connected to a remote server session (e.g., monitoring a live match), the port binding will fail silently or stomp the existing session.
**Fix:** PIE should bind on a random ephemeral port and fail gracefully with an error dialog if binding fails.
**Resolution Phase:** Phase 8 (can be included in any editor task).

---

### TD-19 — `Session::createLocalPair()` port is not documented
**File:** `src/networking/public/networking/Session.h`
**Problem:** `createLocalPair(port)` takes a `uint16_t` port. The port is used for both ends of the loopback pair. Nothing documents which port PIE uses, so two parallel editor instances running PIE on the same machine will collide.
**Fix:** Document the PIE port in `editor_prefs.toml` as a user-configurable value (default 57300). Read it in `PIEController::startPlay()`.
**Resolution Phase:** Phase 8.

---

### TD-20 — `LOG_TRACE` elision rule is not tested
**File:** `cmake/Warnings.cmake`, `src/core/public/core/diag/Logger.h`
**Problem:** `LOG_TRACE` is documented to be elided in Release (`NDEBUG && !ENGINE_DEVREL`). There is no test that validates this — a future change to the macro could silently re-enable trace logging in release builds, adding latency.
**Fix:** Add a compile-time check (`static_assert`) or a unit test that verifies `LOG_TRACE` expands to nothing under `NDEBUG`.
**Resolution Phase:** Phase 9.

---

## Summary Table

| ID | Severity | Description | Phase |
|----|----------|-------------|-------|
| TD-01 | 🔴 | Server doesn't re-clamp pitch from InputFrame | Hotfix |
| TD-02 | 🔴 | fireSerial dedup set never pruned | 9 |
| TD-03 | 🔴 | Lag-comp rewind is placeholder | 9 |
| TD-04 | 🟠 | navmeshAsset field is dead | 9 |
| TD-05 | 🟠 | Static BVH not updated on entity removal | 8/9 |
| TD-06 | 🟠 | Physics solver is single-threaded | 9 |
| TD-07 | 🟠 | EntityFactory archetypes not serializable | 8 (Task #65) |
| TD-08 | 🟠 | GameModeStateBlob size unenforced | 8 (trivial) |
| TD-09 | 🟡 | ColliderShape has no editor widget | 8 (Task #61) |
| TD-10 | 🟡 | SceneSerializer skips unknown components silently | 8 (Task #64) |
| TD-11 | 🟡 | PhysicsMaterialTable has no editor UI | 8 (Task #61) |
| TD-12 | 🟡 | Collision layer matrix has no editor UI | 8 (Task #61) |
| TD-13 | 🟡 | ComponentEditorRegistry requires manual registration | 8 (Task #62) |
| TD-14 | 🟡 | ENGINE_RPC macro has no compile-time validation | 9 |
| TD-15 | 🟡 | SHA-256 asset refs computed without caching | 8 (Task #60) |
| TD-16 | 🟢 | PredictionBuffer slot count is a magic number | 9 |
| TD-17 | 🟢 | SnapshotBuffer extrapolation has no quality fallback | 9 |
| TD-18 | 🟢 | PIE port conflicts on multi-instance | 8 |
| TD-19 | 🟢 | PIE port not user-configurable | 8 |
| TD-20 | 🟢 | LOG_TRACE elision is untested | 9 |
