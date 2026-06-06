# Phase 9 — Risks, Conflicts, and Architecture Issues

**Document type:** Pre-implementation review
**Date:** 2026-06-05
**Status:** Identified during Phase 9 planning. Each item must be resolved (or consciously deferred) before or during the corresponding task.

---

## Index of Issues

| ID | Severity | Category | Short description |
|----|----------|----------|-------------------|
| R01 | 🔴 Critical | Module boundary | MeshRenderSystem lives in `engine::app`; editor cannot use it |
| R02 | 🔴 Critical | Data conflict | `SceneGlobals::spawnPoints` vs `SpawnPointComponent` — two representations |
| R03 | 🔴 Critical | Data conflict | `TriggerEntity` archetype has no matching `TriggerComponent` |
| R04 | 🟠 High | State integrity | PIE stop does not reset PhysicsWorld internal state |
| R05 | 🟠 High | API break | `SceneSerializer::load()` signature change breaks all existing callers |
| R06 | 🟠 High | Executable structure | `FpsGame.exe` vs `Engine.exe` — which binary ships? |
| R07 | 🟠 High | Threading | `TaskScheduler` dependencies across engine modules |
| R08 | 🟠 High | Format versioning | `.easset` v1 → v2 (collision section) backward compatibility |
| R09 | 🟡 Medium | GPU resources | DX12 descriptor heap exhaustion with multiple viewport render targets |
| R10 | 🟡 Medium | Editor UX | Multi-select entity transform command architecture |
| R11 | 🟡 Medium | IGameMode interface | Adding virtual methods breaks all IGameMode implementations |
| R12 | 🟡 Medium | Prefab ordering | Archetype → prefab → instance load order not enforced |
| R13 | 🟡 Medium | Engine physics | Double physics step if both `GameLoop` and `Scene::tick()` call `PhysicsWorld::step()` |
| R14 | 🟡 Medium | Serializer | `SceneSerializer` and `PrefabSerializer` format divergence risk |
| R15 | 🟢 Low | Tech debt | `ENGINE_RPC` macro cannot detect signature mismatch at compile time |
| R16 | 🟢 Low | Editor registry | New components silently invisible in Inspector |

---

## Detailed Risk Analysis

---

### R01 — MeshRenderSystem Module Boundary Violation

**Severity:** 🔴 Critical
**Detected by:** Phase 9 #67 scoping
**Affects tasks:** #66, #67

**Problem:**
`MeshRenderSystem` (`src/app/MeshRenderSystem.h/.cpp`) is in the `engine::app` module. The editor (`EngineEditor.exe`) links `engine::core`, `engine::rendering`, `engine::tools`, and `engine::physics` — but **not** `engine::app`. This means the editor cannot use `MeshRenderSystem` to render the viewport. Currently the viewport renders nothing for mesh entities.

The dependency graph as written prohibits this circular link:
```
app  →  rendering  →  core
         ↑ editor links this
app ← editor would need this (ILLEGAL)
```

**Resolution (task #67):**
Move `MeshRenderSystem` from `engine::app` to `engine::rendering`. Update `engine::app` to import it from there. This is the only structurally sound fix.

**Risk during migration:**
`MeshRenderSystem` currently uses types from `engine::app` (e.g., `GameContext`). Strip those dependencies — use only `engine::core` and `engine::rendering` types — before moving. Any lingering `engine::app` include in `MeshRenderSystem` will create a circular dependency that CMake will not catch at link time on Windows (MSVC static lib linking does not enforce include cycles).

---

### R02 — SceneGlobals::spawnPoints vs SpawnPointComponent

**Severity:** 🔴 Critical
**Detected by:** Phase 9 #68 scoping
**Affects tasks:** #68

**Problem:**
`SceneGlobals` has a `spawnPoints` field (a vector of `Vec3` positions, introduced in Phase 7 spec). `SpawnPointComponent` (new in #68, `kComponentId=14`) is the canonical entity-based spawn point representation. Both exist simultaneously:

- `SceneGlobals::spawnPoints` is serialised in the scene file and visible to `GameLoop`.
- `SpawnPointComponent` entities are ECS entities; queried via `View<SpawnPointComponent>`.
- `IGameMode::selectSpawnPoint()` (new in #68) expects to work with `SpawnPointComponent` entities so it can read team, priority, and exclusion radius.

`SceneGlobals::spawnPoints` cannot carry this metadata and is a dead field — nothing in the runtime reads it after Phase 7 was completed.

**Resolution:**
Remove `SceneGlobals::spawnPoints` as part of #68. This is a serialiser format change; old scenes that had the field will load without error (the unknown-section skip handles it) but the vector data is discarded. New scenes use `SpawnPointComponent` entities exclusively.

**Risk:**
Any code that writes to `SceneGlobals::spawnPoints` must be audited and removed. Search for `spawnPoints` across `src/` before starting #68.

---

### R03 — TriggerEntity Archetype Has No TriggerComponent

**Severity:** 🔴 Critical
**Detected by:** Phase 9 #72 scoping
**Affects tasks:** #72

**Problem:**
`FpsArchetypes.cpp` registers a `TriggerEntity` archetype (Phase 7, task #51). However, there is no `TriggerComponent` — the entity is distinguished only by its archetype name, not by a queryable component. This means:

1. `PhysicsWorld` cannot distinguish trigger volumes from regular physics bodies at runtime.
2. `View<TriggerComponent>` cannot be used to iterate triggers.
3. The overlap detection in #72 has no component to write state to (`entered`, `onEnterFn`).

**Resolution:**
#72 adds `TriggerComponent` (`kComponentId=15`) and updates `registerFpsArchetypes()` to add it to `TriggerEntity`. Existing scene files that have `TriggerEntity` instances will load correctly if `SceneSerializer` applies the archetype via `EntityFactory` (#75) — they will get the default `TriggerComponent` automatically. If #75 is not done before #72, existing scenes will be missing `TriggerComponent` on their trigger entities until resaved.

**Dependency risk:** #72 ideally runs after #75 to avoid this gap in existing scenes. Document this in the task ordering.

---

### R04 — PIE Stop Does Not Reset PhysicsWorld

**Severity:** 🟠 High
**Detected by:** Phase 9 #69 scoping
**Affects tasks:** #69

**Problem:**
`PIEController::restoreSnapshot()` restores ECS component bytes (transforms, velocities, health, etc.) to their pre-play state. However, `PhysicsWorld` (added in #69) maintains its own internal state: body velocity accumulators, broad-phase grid, contact manifolds, constraint islands. Restoring ECS transform bytes does not update PhysicsWorld's internal per-body state.

**Result:** After PIE stop and a second PIE start, physics bodies begin with velocity from the end of the previous play session, not from their initial state.

**Resolution (task #69):**
Add `PhysicsWorld::reset()` which clears all internal state (contact manifolds, island data, dynamic grid) and re-registers all bodies from the current ECS transform/rigidbody data. `PIEController::stopPIE()` calls:
1. `restoreSnapshot()` — restore ECS component bytes.
2. `physicsWorld_->reset()` — rebuild physics state from the now-restored ECS data.

`reset()` is O(n bodies) but acceptable for an editor operation.

---

### R05 — SceneSerializer::load() API Break

**Severity:** 🟠 High
**Detected by:** Phase 9 #75 scoping
**Affects tasks:** #75

**Problem:**
Task #75 changes the signature of `SceneSerializer::load()` to accept an optional `EntityFactory*`. All existing call sites must be updated:
- `tests/tools/SceneSerializerTests.cpp` — multiple test cases.
- `src/editor/EditorApp.cpp` — `openScene()`.
- `src/app/Application.cpp` — start scene loading.
- Any demo that loads a scene.

**Mitigation:**
The `factory = nullptr` default parameter preserves backward compatibility — callers that do not pass a factory get the old raw-byte behaviour. No call site will produce a compile error. The risk is *behavioural* regression: callers that should pass a factory (EditorApp, Application) and don't will silently miss archetype defaults.

**Resolution:**
- Add the default parameter.
- Update EditorApp and Application call sites to pass the factory.
- Add a test that verifies archetype round-trip when factory is provided.
- Leave demo call sites unchanged (they use raw bytes; acceptable for demos).

---

### R06 — FpsGame.exe vs Engine.exe Executable Structure

**Severity:** 🟠 High
**Detected by:** Phase 9 #74 scoping
**Affects tasks:** #74, #66

**Problem:**
Today, `engine.exe` (the `app` module) is the only game executable. Task #74 introduces `FpsGame.exe` (a new `src/game/` module that implements `IGame`). This raises an unresolved architectural question:

**Option A — FpsGame.exe is a separate executable.**
`src/game/` produces `FpsGame.exe` that links `engine::app`, `engine::physics`, `engine::networking`, etc. `engine.exe` (WinMain + bare `Engine`) remains as a demo runner. This is the cleanest separation.

**Option B — FpsGame is compiled into engine.exe.**
`WinMain.cpp` is modified to instantiate `FpsGame` and pass it to `Application`. `src/game/` is a static lib linked into the `app` executable.

**Recommendation:** Option A. `engine.exe` becomes a demo-only runner (or is removed). `FpsGame.exe` is the shipping executable. The `IGame` interface was designed for exactly this separation.

**Risk:** Option A requires a new CMakeLists entry and a new link target. Option B risks WinMain becoming game-specific, blurring the engine/game boundary. If the decision is deferred, task #74 cannot be cleanly implemented.

---

### R07 — TaskScheduler Module Placement

**Severity:** 🟠 High
**Detected by:** Phase 9 #76 scoping
**Affects tasks:** #76

**Problem:**
`TaskScheduler` (new in #76) is placed in `engine::core`. This is correct for the dependency graph (`engine::physics` links `engine::core`). However:
- The animation system (Phase 10) will also want `TaskScheduler`. Correct.
- The rendering module may want it for GPU upload batching. `engine::rendering` already links `engine::core`. Correct.
- The editor uses `engine::core`. Correct.

No dependency graph violation — `engine::core` is the right module. But `TaskScheduler` creates OS threads (Windows `CreateThread` or `std::thread`). This introduces a threading model into `engine::core` which currently has no threads. The `Logger` must be thread-safe before `TaskScheduler` is usable — verify this before enabling parallel narrow-phase.

**Risk:** If Logger is not thread-safe, enabling multi-threaded physics will produce garbled log output or data races on the log buffer. Audit `Logger` before starting #76.

---

### R08 — .easset Format v1 → v2 Backward Compatibility

**Severity:** 🟠 High
**Detected by:** Phase 9 #70 scoping
**Affects tasks:** #70

**Problem:**
Task #70 bumps `.easset` to v2 by appending a collision section. The `loadEasset()` runtime function currently reads exactly the v1 layout. If a v1 file is loaded by v2 code (or vice versa), the reader will misinterpret the file.

**Resolution (task #70):**
Add a version field to the `.easset` header (currently missing). v2 adds:
- `uint8_t version` at byte offset 4 of the header.
- `uint8_t hasCollision` flag.
- If `hasCollision == 1`, a `CollisionSection` follows the mesh section.

The v2 loader checks the version byte:
- v1 files: no version byte at offset 4 (or the byte is part of something else). **The current v1 format must be audited** to determine whether adding a version byte is backward-compatible or requires a different approach (magic-based versioning, section offsets).

**Recommendation:** If v1 has no version field, define v2 as: `magic[4] + version(1) + meshSectionOffset(4) + collisionSectionOffset(4) + ...`. Old v1 files that lack this header are detected by magic mismatch or version == 0 and treated as legacy. `loadEasset()` falls back to the v1 reader for legacy files. Run `asset_cooker` on all existing `.easset` files to re-export them as v2.

---

### R09 — DX12 Descriptor Heap Exhaustion with Multiple RTs

**Severity:** 🟡 Medium
**Detected by:** Phase 9 #67 scoping
**Affects tasks:** #67

**Problem:**
Task #67 adds an offscreen render target (`ViewportRT`) per viewport panel. Each RT needs:
- 1 RTV (render target view) descriptor.
- 1 SRV (shader resource view) descriptor (so ImGui can display it as a texture).
- 1 depth buffer DSV (depth stencil view).

If the editor opens multiple viewport panels (or if a thumbnail renderer creates many per-frame), the static descriptor heaps allocated at `GpuDevice` startup may overflow. The current heap sizes were sized for a single swapchain.

**Resolution:**
Before adding the offscreen RT, check the RTV/DSV/SRV heap capacities in `GpuDevice` initialization. Increase as needed. A single viewport RT + thumbnail cache of up to 64 entries requires ~70 additional SRV slots and ~2 additional RTV/DSV slots — typically within margin, but verify the numbers against the current heap allocation.

---

### R10 — Multi-Select TransformCommand Architecture

**Severity:** 🟡 Medium
**Detected by:** Phase 9 #71 scoping
**Affects tasks:** #71

**Problem:**
The existing `TransformCommand` stores `(entityId, oldTransform, newTransform)` — one entity, one command. Multi-select (Ctrl+drag gizmo) moves N entities simultaneously. The undo stack should undo all N moves in one step, not N separate steps.

**Options:**

**Option A — MultiTransformCommand:** New command type that stores a vector of `(entityId, oldTransform, newTransform)`. `execute()` and `undo()` iterate the vector. Clean but adds a new command type.

**Option B — Compound command (macro record):** Begin/End recording that groups N `TransformCommand` pushes into one undo group. More general but more complex.

**Recommendation:** Option A for Phase 9. `MultiTransformCommand` is simple and covers the gizmo case. Compound commands are Phase 10.

**Risk:** The centroid gizmo used for multi-select needs to compute the group centroid and apply relative offsets to each entity. This is straightforward but the centroid must be computed *before* the drag starts and stored in the command, not recomputed on undo.

---

### R11 — IGameMode Interface Additions Break Implementations

**Severity:** 🟡 Medium
**Detected by:** Phase 9 #68, #72 scoping
**Affects tasks:** #68, #72

**Problem:**
Tasks #68 and #72 add virtual methods to `IGameMode`:
- `selectSpawnPoint(uint32_t teamId, const View<SpawnPointComponent>& candidates) -> EntityId`
- `onTriggerEnter(EntityId trigger, EntityId entity)`
- `onTriggerExit(EntityId trigger, EntityId entity)`

Any existing `IGameMode` implementation (`DeathMatchMode` from #74, plus any user-defined game modes) must be updated to implement these methods or they will fail to compile (if pure virtual) or silently not work (if they have default implementations).

**Resolution:**
Provide default implementations for all new `IGameMode` virtual methods:
```cpp
virtual EntityId selectSpawnPoint(uint32_t, const View<SpawnPointComponent>&) {
    return kInvalidEntity; // caller falls back to random selection
}
virtual void onTriggerEnter(EntityId, EntityId) {}
virtual void onTriggerExit(EntityId, EntityId) {}
```
Default implementations are safe to override. Callers must handle `kInvalidEntity` from `selectSpawnPoint()`.

---

### R12 — Archetype → Prefab → Instance Load Order Not Enforced

**Severity:** 🟡 Medium
**Detected by:** Phase 9 #75 scoping
**Affects tasks:** #75, #65 (Phase 8 prefab)

**Problem:**
The 3-layer override stack (archetype → prefab → instance) is correct in theory but relies on `SceneSerializer::load()` applying layers in a specific order. If `PrefabSerializer::instantiate()` is called after the stored instance overrides are applied (instead of before), the prefab defaults will overwrite instance overrides — reversing the correct behaviour.

**Resolution:**
The load order in task #75, section 3, is explicit:
1. `EntityFactory::spawn(archetypeName)` — archetype defaults.
2. `PrefabSerializer::instantiate(prefabData)` — prefab defaults on top of archetype.
3. `world.addComponentRaw(e, typeId, bytes)` for each stored override.

This order must be enforced and tested. A test case must verify that an instance override takes precedence over the prefab default, which takes precedence over the archetype default.

---

### R13 — Double Physics Step (GameLoop + Scene::tick)

**Severity:** 🟡 Medium
**Detected by:** BW analysis during Phase 8 (noted as resolved); risk re-emerges in Phase 9
**Affects tasks:** #68, #69

**Problem:**
`GameLoop::serverTick()` calls `physics_->step(dt)` (BW1, resolved in Phase 8). `Scene::tick()` also has a `physicsStepFn_` delegate which the editor sets during PIE (#69). If both are active simultaneously:
- Runtime path: `GameLoop::serverTick()` → `physics_->step()`. `Scene::tick()` has no `physicsStepFn_` set (it's `nullptr` by default). Safe.
- PIE path: `PIEController::tick()` → `scene_->tick()` → `physicsStepFn_()`. `GameLoop` is not running during PIE. Safe.

**Risk:** If `Application` is modified to call both `GameLoop` and `Scene::tick()` (e.g., for the `tickActive()` path introduced in BW wires), and `Scene::physicsStepFn_` is inadvertently set in the runtime path, physics will step twice per frame.

**Resolution:** Add an `ENGINE_ASSERT(physicsStepFn_ == nullptr, "PhysicsWorld::step() called twice — check GameLoop and Scene::tick() are not both stepping physics")` in `PhysicsWorld::step()` if called more than once per logical tick. The simplest guard: a `bool steppedThisFrame_` that is reset in `beginFrame()`.

---

### R14 — SceneSerializer and PrefabSerializer Format Divergence

**Severity:** 🟡 Medium
**Detected by:** Phase 9 #75 scoping
**Affects tasks:** #75

**Problem:**
`SceneSerializer` and `PrefabSerializer` (Phase 8) both write binary component blobs but with different headers and section layouts. The v2 archetype round-trip format (#75) adds `hasArchetypeName` and `hasPrefabRef` flags to the entity table row. These flags must be consistent between the two serialisers.

**Risk:** If `PrefabSerializer` writes entity rows in v1 format (no flags) and `SceneSerializer::load()` with #75 changes now expects the v2 row format, loading a prefab that was saved before the v2 upgrade will fail silently (wrong byte offsets).

**Resolution:** Version both serialisers together. When #75 ships the v2 entity row format, `PrefabSerializer` must also write v2 rows. Add a format version byte to both file headers. Test: load a v1 scene file with the v2 loader; verify it loads correctly and entities have no archetype name set.

---

### R15 — ENGINE_RPC Signature Mismatch Is a Silent Runtime Crash

**Severity:** 🟢 Low
**Task:** #79
**Description:** See `task-79-rpc-validation.md`. Resolved by migrating to `registerRpc<Fn>()` template with `static_assert`. No architectural change required.

---

### R16 — New Components Silently Invisible in Inspector

**Severity:** 🟢 Low
**Task:** #78
**Description:** See `task-78-registry-autodiscovery.md`. Resolved by `EditorApp::validateComponentRegistry()` assertion at startup. No architectural change required.

---

## Cross-Cutting Dependencies Between Risks

The following risks are interconnected and should be resolved in dependency order:

```
R01 (MeshRenderSystem module)
    └── must be resolved before #67 can produce a working viewport

R02 (spawnPoints conflict)
    └── must be resolved (remove field) before #68 ships

R03 (TriggerEntity/TriggerComponent)
    └── resolves when #72 ships, but existing scenes need #75 for retroactive fix
    └── R03 depends on R05 (SceneSerializer API) being resolved first

R05 (SceneSerializer API break)
    └── must be resolved before #75 ships
    └── R12 depends on R05 being correct

R06 (executable structure)
    └── must be decided before #74 starts

R13 (double physics step)
    └── latent — add the ENGINE_ASSERT guard in #69 to make the risk visible early
```

---

## Summary: Recommended Pre-Phase-9 Actions

Before any Phase 9 task starts, these decisions must be made and documented:

1. **R06 decision required:** Is `FpsGame.exe` a separate binary, or does it compile into `engine.exe`? Block #74 until decided.
2. **Audit `SceneGlobals::spawnPoints` usage (R02):** `grep -r spawnPoints src/` — confirm it's dead and safe to remove. Include in #68 start criteria.
3. **Audit Logger thread-safety (R07):** Before #76 creates worker threads, verify `Logger` uses a mutex or lock-free queue. This is a blocking prerequisite.
4. **Check `.easset` v1 header layout (R08):** Open `src/tools/public/tools/assetcooker/` or wherever the v1 format is documented; verify whether a version byte can be added non-destructively. Include in #70 start criteria.
5. **Check descriptor heap sizes (R09):** Grep for heap capacity constants in `GpuDevice.cpp`; confirm margin before #67 adds offscreen RTs.
