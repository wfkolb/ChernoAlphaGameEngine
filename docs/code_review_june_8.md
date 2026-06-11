# Engine Code Review — June 8, 2026

**Reviewer:** Claude Sonnet 4.6  
**Scope:** Phases 1–9 complete codebase (engine/ + FPSGame/)  
**Reviewed against:** CLAUDE.md architecture spec, docs/architecture.md

---

## Executive Summary

The engine is in a healthy state for a Phases 1–9 deliverable. The ECS archetype graph, FrameGraph barrier management, and networking reliability layer are all well-structured and mostly correct. The most significant findings are:

1. **Critical (1):** The depth buffer is cleared to `1.0f` in `GpuDevice.cpp` but the engine convention is reverse-Z (clear to `0.0f`). The `DepthFunc = GREATER_EQUAL` relies on far-plane depth being `0.0f`. Clearing to `1.0f` will cause the depth test to reject all geometry on some hardware configurations.
2. **Major (6):** ECS `View` captures archetypes at construction time — structural changes during iteration invalidate the snapshot silently. The `FpsCameraSystem` implements the camera logic independently from `Camera.cpp::fpsCameraUpdate`, creating a sign-divergence in yaw accumulation. The physics `step` solver uses a single-iteration impulse with no position correction limiting, which can explode with high-mass-ratio pairs. The `SnapshotEncoder` does not delta-compress; it sends full component state every 20 Hz tick. The trigger overlap test uses entity `.index` for removal but `Entity` comparison for enter/exit which is generation-unsafe. The `loadEasset` fallback to a unit cube described in CLAUDE.md is not actually implemented.
3. **Minor (many):** See per-section detail below.

**Total findings:** 1 Critical, 6 Major, 14 Minor.

---

## 1. ECS (src/core/ecs/)

### What is working well

- The archetype graph with add/remove edge pointers (`addEdge[id]` / `removeEdge[id]`) is correctly populated on demand, giving O(1) archetype transitions after the first traversal.
- `CommandBuffer::flush` properly builds a `pendingMap` to resolve deferred entity handles before applying commands, and it correctly ignores double-add and double-remove cases.
- `World::destroyEntity` calls component destructors before swap-removing the row, which is the right order to avoid use-after-free in destructor callbacks.
- `World::moveEntity` copies shared components before the swap-remove and correctly patches the moved entity's `row` in the entity record array.
- The generation counter overflow (32-bit wraparound) is benign given realistic entity counts.

### Bugs and incomplete implementations

**Major — View snapshot staleness**  
File: `src/core/public/core/ecs/View.h`, lines 13–23 (constructor)

`View<T...>` captures the `matching_` archetype list at construction. If any code path runs `CommandBuffer::flush` or calls `World::addComponent` / `World::destroyEntity` while a `View` iterator is live (e.g., a system that defers work but then calls flush mid-iteration on a nested system), the pointer list becomes stale. The iterator holds raw `Archetype*` pointers which can be invalidated if `archetypes_` reallocates (it is a `std::vector<std::unique_ptr<Archetype>>`). The pointers themselves are stable since `unique_ptr` owns the archetypes and the vector growing only copies the `unique_ptr`, but newly added archetypes that match the query mask will not be visited. The comment in `View::Iterator::skipEmpty` makes this assumption implicit rather than documented. No assert or documentation warns callers. This is consistent with ECS conventions but is a gap for documentation/assertion.

**Minor — `addComponent` bypasses alignment**  
File: `src/core/public/core/ecs/World.inl`, line 51

`std::memcpy(col.data() + newRow * sizeof(T), &value, sizeof(T))` assumes the vector storage is aligned to `alignof(T)`. `std::vector<uint8_t>` guarantees alignment for `uint8_t` (1-byte) but not for larger types. For SIMD or extended-alignment types (`alignas(16)`) this is undefined behaviour. `archetypeAppendRow` resizes the `uint8_t` vector, which guarantees allocation alignment of `std::max_align_t` (~16 bytes for MSVC heap), so in practice this is safe for all current component types, but should be guarded with `static_assert(alignof(T) <= alignof(std::max_align_t))` in the template.

**Minor — `World::registry_` is a global inline static**  
File: `src/core/public/core/ecs/World.h`, line 89

`static inline std::array<ComponentMeta, 256> registry_` is shared across all `World` instances and is never reset. If a test creates multiple `World` instances and calls `registerComponent` twice with the same ID but different metadata (e.g., different `size`), the second call silently overwrites the first. This is consistent with the intended "register once at startup" contract but there is no assert to enforce uniqueness.

**Minor — `CommandBuffer::flush` accessing private `World` members directly**  
File: `src/core/ecs/CommandBuffer.cpp`, lines 64–89

The `flush` method accesses `World::registry_`, `World::entities_`, and calls `world.getOrCreateArchetype` (private). This is accomplished via `friend class CommandBuffer` in `World.h`. The friendship is documented, but the implementation in `CommandBuffer.cpp` duplicates the add-component logic that already exists in `World::addComponent` and `World::addComponentRaw`. Any future change to the add-component path needs to be replicated here. Consider factoring out a private `World::addComponentById` helper.

---

## 2. Frame Graph (src/rendering/FrameGraph.h + .cpp)

### What is working well

- The known double-barrier fix is present and correct: `FrameGraph::execute` (line 314) explicitly skips `isBackBuffer` resources in the post-execute barrier loop, delegating the `RENDER_TARGET→PRESENT` transition to `GpuDevice::endFrame`. This matches the documented fix in CLAUDE.md.
- `PassBuilder::import` correctly calls `AddRef` on the imported resource and the matching `Release` happens when the `ComPtr` is destroyed in `reset()`.
- Transient resources are lazily allocated in `compile()` with correct `D3D12_CLEAR_VALUE` structs.
- The `PassResources::Impl` is stack-local per `execute()` call, avoiding per-frame heap allocation.

### Bugs and incomplete implementations

**Minor — Post-execute barrier assumes all non-back-buffer imported resources want `DEPTH_WRITE`**  
File: `src/rendering/FrameGraph.cpp`, lines 316–328

```cpp
D3D12_RESOURCE_STATES targetState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
```

This hardcodes the "return to" state as `DEPTH_WRITE` for every imported resource that is not the back buffer. If a future pass imports a texture for reading (e.g., an IBL cubemap imported as `PIXEL_SHADER_RESOURCE`) and that texture's current state after the pass is still `PIXEL_SHADER_RESOURCE`, no barrier is emitted (the `== targetState` check prevents it). But if the texture ends up in some intermediate state, the post-execute barrier would incorrectly transition it to `DEPTH_WRITE`. The fix is to store a "desired end-of-frame state" per imported resource and use that instead of the hardcoded constant.

**Minor — `VirtualResource` holds a `TextureDesc` for imported resources**  
File: `src/rendering/internal/FrameGraphImpl.h`, lines 17–27

The `TextureDesc desc` field is zero-initialized for imported resources (e.g., back buffer, depth buffer imported via `importBackBuffer`). The `flagsNeedRtv` / `flagsNeedDsv` checks in `compile()` read `vr.desc.resourceFlags`, which is `0` for imported resources. This is correct (the condition `!vr.isImported` guards creation), but the `TextureDesc` field wastes 44 bytes per imported resource and could be replaced with a union or optional.

**Minor — `compile()` is called every frame for transient resources but does not release old allocations**  
File: `src/rendering/FrameGraph.cpp`, lines 168–258

`compile()` checks `if (!vr.isImported && !vr.resource)` before allocating; after `reset()`, `impl_->resources` is cleared, so all transient resources are re-allocated each frame. The `CreateCommittedResource` call on every frame tick is expensive. This is a known Phase 10 item (resource pooling) but should be tracked.

**Minor — `PassBuilder::read` and `PassBuilder::write` are identical**  
File: `src/rendering/FrameGraph.cpp`, lines 60–69

Both `read` and `write` push to `pass->barriers` with the supplied `requiredState`. There is no semantic distinction: the graph does not compute write hazards separately from read hazards. This means two passes that both declare `write(h, RENDER_TARGET)` will not generate a UAV barrier or a write-after-write detection. For the current single-queue linear graph this is safe, but a future async compute extension will need proper read/write tracking.

---

## 3. Physics (src/physics/)

### What is working well

- The parallelised broad/narrow phase partitioning across `TaskScheduler` worker threads is correct: each worker writes to its own `perTaskContacts[t]` slice, eliminating data races, and `scheduler.wait()` provides the synchronisation barrier before the merge.
- The BVH rebuild is only triggered when `staticDirty` is set, avoiding unnecessary rebuilds on frames with no static-body changes.
- `CharacterControllerImpl` correctly implements coyote-time, jump buffering, and surface-slide projection.
- Trigger enter/exit events are driven by set-difference between `currentOverlaps` and `activeOverlaps`, which is correct.

### Bugs and incomplete implementations

**Major — Depth-buffer clear value is incorrect for reverse-Z**  
File: `src/rendering/GpuDevice.cpp`, lines 204–207

```cpp
clearVal.DepthStencil.Depth  = 1.0f;   // BUG: should be 0.0f for reverse-Z
```

The engine convention (CLAUDE.md, Fixed Bugs section) states: "Depth: Reverse-Z, near=1.0, far=0.0; `DepthFunc = GREATER_EQUAL`; clear depth to 0.0f." The depth buffer is created and cleared with `1.0f`. In reverse-Z, `0.0f` represents the far plane and `1.0f` represents the near plane. Clearing to `1.0f` means every pixel starts at maximum "closeness", so the `GREATER_EQUAL` test accepts only the very first triangle rendered (depth > 1.0 can never happen). In practice GPU validation may catch this or geometry may appear partially, but this is a **Critical** correctness issue. The resize path in `beginFrame` (same file around line 302) also sets `cv.DepthStencil.Depth = 0.0f` — note the inconsistency between creation (1.0f) and resize (0.0f), confirming the initial creation is the bug.

*(Note: This finding is filed under Physics for organisational reasons but the source is GpuDevice.cpp.)*

**Major — Single-iteration impulse solver with unbounded Baumgarte correction**  
File: `src/physics/PhysicsWorld.cpp`, lines 402–419

The impulse solver runs one iteration only. The Baumgarte correction:

```cpp
const float correction = kBaumgarte * std::max(ci.depth - kSlop, 0.0f) * invMass;
b.position += ci.normal * correction;
```

multiplies by `invMass` rather than dividing by the effective mass of the contact (which would be `1 / (invMassA + invMassB)`). For a very light body (small mass, large invMass) penetrating a heavy static, this pushes the light body by an amount `kBaumgarte * depth / mass`. With a mass of 0.01 kg the correction can be ~4m per contact per step, which can launch bodies. The standard formula should use `effective_mass = 1.0f / (invMassA + invMassB)` with `invMassB = 0` for statics, giving `correction = kBaumgarte * depth * invMassA / (invMassA)` = `kBaumgarte * depth`, independent of mass. Fix: remove `* invMass` from the correction term.

**Major — Gravity is not applied to character controllers**  
File: `src/physics/PhysicsWorld.cpp`, lines 428–457

The character controller step (step 5) reads `desiredVelocity` set by the caller (via `setDesiredVelocity`) and applies it. Gravity is accumulated only in step 1, which skips `isCharacterController` bodies. The `clampGroundedVelocity` helper (CharacterControllerImpl.cpp line 30) preserves the Y component of `desiredVelocity` when not grounded, but the caller (FpsGame/CharacterSystem) must manually add gravity to `desiredVelocity.y` each tick. This contract is not documented in the header. If the FPSGame's character system does not subtract gravity from `desiredVelocity.y` every frame, characters float in mid-air. The design should either apply gravity internally or add an explicit `ENGINE_ASSERT` or comment requiring the caller to handle it.

**Minor — `sweepCapsule` normal approximation**  
File: `src/physics/PhysicsWorld.cpp`, line 574

```cpp
best.normal = -dir; // approximate
```

For character controller ground checks (`{0,-1,0}` direction), this produces `{0,1,0}` regardless of the actual surface. The `isGrounded` check then compares `ground.normal.y >= cosMax` which trivially passes for flat floors but the `slideAlongSurface` call uses this normal. For sloped surfaces the slide direction will be wrong. The actual surface normal should be computed from the nearest point on the static geometry.

**Minor — Trigger overlap test uses `.index` for removal but full `Entity` for enter/exit**  
File: `src/physics/PhysicsWorld.cpp`, lines 622–635

`removeTrigger` erases from `activeOverlaps` by checking `it->second.triggerEntity.index == entity.index`, which ignores the generation field. If a trigger entity is destroyed and a new entity is created at the same index, stale overlap records referencing the old entity would be removed erroneously. The comparison should use the full `Entity` equality operator (index + generation).

---

## 4. FPS Camera System (src/rendering/FpsCameraSystem.cpp)

### What is working well

- Pitch clamping to ±89° is correct and present in both the `FpsCameraSystem::tick` and the free function `fpsCameraUpdate`.
- The YXZ Euler order (`fromEulerYxz`) is correct for FPS-style look (yaw applied to world Y first, then pitch around local X).
- Sprint multiplier (Shift × 5) and Q/E vertical movement are correctly implemented.

### Bugs and incomplete implementations

**Major — Yaw sign divergence between `FpsCameraSystem` and `fpsCameraUpdate`**  
Files: `src/rendering/FpsCameraSystem.cpp` line 30; `src/rendering/Camera.cpp` line 47

`FpsCameraSystem::tick` accumulates yaw as:
```cpp
ctrl.yaw -= dx;  // negative: right mouse = negative yaw
```

`Camera.cpp::fpsCameraUpdate` accumulates yaw as:
```cpp
ctrl.yaw += dx;  // positive: right mouse = positive yaw
```

These two implementations produce mirrored horizontal rotation. Any code that calls `fpsCameraUpdate` directly (the free function, e.g., via the editor or unit tests) will rotate in the opposite horizontal direction from calling `FpsCameraSystem::tick`. One of these must be wrong. The comment in `FpsCameraSystem.cpp` line 29 says "RH Y-up: turning right = negative rotation around +Y" which is correct for right-handed Y-up, meaning `ctrl.yaw -= dx` is the correct sign. The `Camera.cpp` version is the bug.

**Minor — `eyeHeight` field is declared but never applied**  
File: `src/rendering/public/rendering/Camera.h`, line 34

`FpsCameraController` has an `eyeHeight` field (default 1.7m). Neither `FpsCameraSystem::tick` nor `fpsCameraUpdate` adds `eyeHeight` to `transform.position.y`. The camera position is read directly from the entity's `Transform`. If the character controller positions the entity at the capsule base, the camera renders from floor level. This is a known gap for Phase 10 (integrate CharacterController + camera), but should be noted as a non-functional field.

**Minor — `FpsCameraSystem.h` is duplicated**  
Files: `src/rendering/public/rendering/FpsCameraSystem.h` and `src/rendering/FpsCameraSystem.h`

Two header files declare `class FpsCameraSystem` in different include paths. The internal one (`src/rendering/FpsCameraSystem.h`) includes the public one via `#include <rendering/FpsCameraSystem.h>` — or they may define independent declarations. This duplication is error-prone and will cause ODR issues if they diverge. One should be deleted.

---

## 5. Networking (src/networking/)

### What is working well

- `ReliableChannel` correctly implements sliding-window ack with a 32-bit bitfield (covers 33 sequence numbers) and 100ms resend timer. The sequence wrap-around arithmetic (`uint16_t` subtraction mod 65536) is correct and handles the "older vs. newer" split at 32767.
- `processAcks` correctly removes entries from `resendQueue_` when the remote's ack header covers their sequence number, and is safe against aliased acks for very old entries (beyond `kAckBitfieldBits`).
- `SnapshotEncoder`/`SnapshotDecoder` round-trips cleanly: varint encoding, 32-byte ComponentMask, and LE generation bytes are all correctly matched between encode and decode.
- `ReplicationSystem` smallest-three quaternion encoding is mathematically correct, including the sign normalisation step that ensures the omitted component is implicitly positive.

### Bugs and incomplete implementations

**Major — `ReplicationSystem` sends full state every snapshot, no delta compression**  
File: `src/networking/ReplicationSystem.cpp`, lines 127–153

`buildSnapshot` always emits `dirtyMask = rcb` (all replicated components) regardless of whether the values changed since the last acknowledged snapshot. The header stores `ackedSeq_` for the purpose of delta compression but it is never used to compare against a baseline. At 20 Hz with 64 entities and 24 bytes per entity, that is ~30 KB/s before UDP overhead — fine for LAN but will saturate a 56 Kbps mobile link. This is a Phase 10 item but the architecture already has the `ackedSeq_` / ring buffer infrastructure for delta compression. The `[[maybe_unused]]` annotation on `decodeTransform` (line 56) confirms the decode path is not yet wired up on the client.

**Minor — `ReliableChannel::resendUnacked` does not update `sentTimeMs` atomically**  
File: `src/networking/ReliableChannel.cpp`, lines 148–153

```cpp
entry.sentTimeMs = now;
```

The `sentTimeMs` is updated after sending. If `socket_.send` returns before the clock advances (e.g., ultra-fast loopback), the entry immediately qualifies for another resend on the next `poll` call because `now - sentTimeMs` could be `0 < kResendWindowMs`. In practice the 100ms window makes this benign, but the update should happen before `socket_.send` or be separated from the condition check.

**Minor — `Session::createLocalPair` binds to IPv4 loopback only**  
File: `src/networking/Session.cpp`, lines 43–63

The function uses `Socket::createUdp(/*dualStack=*/false)` and constructs IPv4-mapped loopback endpoints. The comment explains this avoids dual-stack address-mismatch on Windows loopback. However, the constructor for production use (outside test pairs) presumably allows dual-stack, so the session API is not symmetric. This is acceptable for test use but should be explicitly documented as "for testing only" (it is, in the header comment).

**Minor — Snapshot `SnapshotAck` struct is defined but not used**  
File: `src/networking/public/networking/Snapshot.h`, lines 40–43

`struct SnapshotAck { uint32_t seq; uint32_t receivedMs; }` is declared but there is no corresponding encode/decode function and it is not sent anywhere in the networking stack. Dead code, can be removed or completed.

---

## 6. Asset Pipeline (src/tools/)

### What is working well

- `loadEasset` validates every bounds check before memcpy (magic, version, assetType, section offsets, expected section size), correctly rejecting truncated or mismatched files.
- Version 1 (mesh only), version 2 (+ collision), and version 3 (+ textures) are cleanly handled with separate code paths.
- `loadIblEasset` correctly handles partial IBL assets (CMAP only, BRDF only, or both) and returns `nullopt` only when neither section parses.
- The TEX section iterates mips safely: each mip header is bounds-checked before the pixel data copy.
- `static_assert` on all packed structs is present and provides compile-time layout guarantees.

### Bugs and incomplete implementations

**Major — `loadEasset` returns `nullopt` on failure; CLAUDE.md says "falls back to unit cube"**  
File: `src/tools/public/tools/EassetLoader.h`, line 81

CLAUDE.md states: "Runtime: `loadEasset()` returns `optional<CpuMesh>`; falls back to unit cube on failure." The current API returns `std::nullopt`; no fallback is provided. Each call site must provide its own fallback, but no call site was observed doing so in the reviewed code. This mismatch between documentation and implementation will cause silent rendering failures (empty mesh handles) when an asset file is missing or corrupt rather than the documented unit-cube substitute. Either the documentation should be updated or a wrapper `loadEassetOrCube()` should be provided.

**Minor — `AssetImporter.cpp` defines `struct VertexStatic` locally; differs from `rendering::VertexStatic`**  
File: `src/tools/AssetImporter.cpp`, lines 73–80

The local `VertexStatic` struct matches the rendering type by layout (28 bytes, same fields), but because it is defined in the anonymous namespace it is a separate type. A `static_assert` comparing sizes would prevent silent layout drift if `rendering::VertexStatic` changes in the future. Currently they are consistent (28 bytes each) but the duplication is fragile.

**Minor — `EassetLoader.cpp` and `AssetImporter.cpp` both define the same packed structs independently**  
Files: `src/tools/EassetLoader.cpp` and `src/tools/AssetImporter.cpp`

`EassHeader`, `TocEntry`, `MeshSectionHeader`, `CollSectionHeader`, `TexSectionHeader`, `TexMipHeader` are duplicated with identical layout between the two files. If the binary format ever changes, both copies must be updated. These should be extracted to an `internal/EassetFormat.h` shared header.

---

## 7. FPSGame Integration (FPSGame/src/)

### What is working well

- `FpsGame::onInit` correctly looks for `project.toml` in CWD then the exe directory and falls back to defaults on parse failure.
- `FpsGame::onGameTick` calls `fpsCamSys.tick(*ctx.world, dt)` — the ECS-based camera system is properly wired into the game tick.
- `ProjectConfig` parsing uses toml++ and is robust to missing keys (all reads use the `.value<T>()` optional pattern).
- The debug UI (`onDebugUI`) is correctly gated on `ENGINE_DEVREL`.

### Bugs and incomplete implementations

**Major — No input→CharacterController wiring in `FpsGame`**  
File: `FPSGame/src/FpsGame.cpp`, entire file

`FpsGame::onGameTick` only calls `fpsCamSys.tick`, which handles the floating camera entity. There is no system that reads `InputSystem` state and calls `physicsWorld.setDesiredVelocity()` for the player's `CharacterController`. The `CharacterController.desiredVelocity` will remain `{0,0,0}` every frame, so the player entity never moves. A `CharacterMovementSystem` that maps WASD input to `desiredVelocity` (with gravity accumulation, as noted in Section 3) is entirely absent.

**Minor — `FpsGame::onShutdown` is empty**  
File: `FPSGame/src/FpsGame.cpp`, line 112

Scene unload, physics world teardown, and any network session cleanup are not called in `onShutdown`. If `GameContext` owns these objects, they may be destroyed in an incorrect order. This should at minimum call `ctx.sceneManager->unload(cfg.startScene)` and document the ownership contract.

**Minor — `SceneManager::load` returns `nullptr` if scene already loaded**  
File: `src/core/scene/SceneManager.cpp`, line 24

```cpp
if (findEntry(name)) return nullptr; // already exists
```

`FpsGame::onInit` treats a `nullptr` return as "not found" and logs a warning, but `nullptr` also means "already loaded". If the game restarts or `onInit` is called twice (e.g., in PIE), the second call will warn that the scene was not found when in fact it is active. The return value should distinguish "already loaded" from "file not found".

**Minor — `startScenePath` in `FpsGameMain.cpp` duplicates the value from `project.toml`**  
File: `FPSGame/src/FpsGameMain.cpp`, line 17

```cpp
desc.startScenePath = "scenes/main.scene";
```

`ProjectConfig` also has `startScene = "scenes/main.scene"`. If the project.toml changes the start scene, the hardcoded value in `WinMain` will not be updated. The `ApplicationDesc::startScenePath` should be populated from `ProjectConfig::loadFrom` after the game is constructed, or removed if `FpsGame::onInit` already handles scene loading (which it does).

---

## 8. Lighting (src/rendering/)

### What is working well

- `buildLightArray` correctly packs `GpuLight` structs into a flat byte array matching the HLSL `GpuLight` layout (64 bytes verified by `static_assert`).
- Spot light `innerCosAngle` / `outerCosAngle` / `invDiff` encoding is correct and matches the HLSL decode in `OpaquePS.hlsl`.
- The directional light direction uses `+Z` in local space rotated to world, which matches the conventions used in the shadow cascade matrix construction.
- `OpaquePS.hlsl` correctly evaluates all three light types (directional, point, spot) using Cook-Torrance GGX and falls back to flat ambient when IBL is not bound.

### Bugs and incomplete implementations

**Minor — Directional light's forward vector uses `+Z`, inconsistent with camera convention**  
File: `src/rendering/LightCullSystem.cpp`, line 62

```cpp
const core::math::Vec3 forward =
    core::math::rotate(transform.rotation, core::math::Vec3{0.0f, 0.0f, 1.0f});
```

The engine convention (CLAUDE.md) is `+Z forward` for entity transforms. However, `cameraViewMatrix` uses `-Z` as the look direction (`forward = rotate(q, {0,0,-1})`). Light and camera forward conventions are opposite. Whether this is intentional (light illuminates in the direction the entity "faces" at +Z) or a bug depends on how art-content is authored, but it should be explicitly documented to avoid confusion.

**Minor — `gLights` declared as `StructuredBuffer` but `GpuLightData::bytes` is a raw byte array**  
File: `src/rendering/internal/LightCullSystem.h` / `engine/shaders/opaque/OpaquePS.hlsl`

The CPU side stores lights as `uint8_t bytes[kMaxLights * 64]`. The HLSL shader reads them as `StructuredBuffer<GpuLight>`. The upload path (not reviewed, presumably in `MeshManager` or the render pass) must correctly sub-allocate a structured buffer and set `StructureByteStride = 64`. If the upload path instead creates a raw buffer or sets the wrong stride, the shader will read garbage. This deserves an explicit comment on the CPU side tying `kMaxLights * 64` to the StructuredBuffer layout.

**Minor — `OpaquePS.hlsl` does not sample the ambient occlusion texture**  
File: `engine/shaders/opaque/OpaquePS.hlsl`

The `GpuMaterial` type (visible via `CommonTypes.hlsli`) presumably has an AO texture index. The PBR ambient term `ambient = diffuseIBL + specularIBL` does not multiply by AO, meaning baked occlusion data in glTF metallic-roughness AO channel is silently discarded. This is a quality gap rather than a correctness bug.

**Minor — Shadow depth convention is not reverse-Z for CSM**  
File: `engine/shaders/opaque/OpaquePS.hlsl`, line 39

```cpp
// Shadow maps use standard depth [0,1] (not reverse-Z).
```

The comment documents that shadow maps intentionally use standard depth (near=0, far=1) despite the main framebuffer using reverse-Z. The `SamplerComparisonState gShadowPCF` uses `ComparisonLessEqual` which is correct for standard depth but would be wrong for reverse-Z. This is a deliberate design choice, but the shadow pass must create its DSV with a different clear value (1.0f, not 0.0f) and use a different depth-test PSO than the opaque pass. This should be verified in `ShadowPass.cpp` (not reviewed here) to confirm the intentional inconsistency is maintained end-to-end.

---

## Summary Table

| # | Severity | Area | Description | File:Line |
|---|----------|------|-------------|-----------|
| 1 | **Critical** | Rendering | Depth buffer cleared to 1.0f instead of 0.0f (reverse-Z violation) | `GpuDevice.cpp:204` |
| 2 | **Major** | Physics | Baumgarte position correction multiplies by `invMass`, can explode with light bodies | `PhysicsWorld.cpp:417` |
| 3 | **Major** | Physics | Gravity not applied to CharacterController bodies; contract undocumented | `PhysicsWorld.cpp:428` |
| 4 | **Major** | Rendering | Yaw sign divergence: `FpsCameraSystem::tick` uses `-=`, `fpsCameraUpdate` uses `+=` | `FpsCameraSystem.cpp:30`, `Camera.cpp:47` |
| 5 | **Major** | Networking | ReplicationSystem sends full state every tick; `ackedSeq_` delta compression never used | `ReplicationSystem.cpp:127` |
| 6 | **Major** | Asset Pipeline | `loadEasset` returns nullopt on failure; documented fallback to unit cube not implemented | `EassetLoader.h:81` |
| 7 | **Major** | FPSGame | No CharacterController movement system; player entity never moves | `FpsGame.cpp` (entire file) |
| 8 | Minor | ECS | `View` snapshot captured at construction; new matching archetypes not visited | `View.h:13` |
| 9 | Minor | ECS | `addComponent` does not assert `alignof(T) <= alignof(std::max_align_t)` | `World.inl:51` |
| 10 | Minor | ECS | `CommandBuffer::flush` duplicates add-component logic instead of using a shared helper | `CommandBuffer.cpp:59` |
| 11 | Minor | FrameGraph | Post-execute barrier hardcodes `DEPTH_WRITE` for all non-back-buffer imports | `FrameGraph.cpp:316` |
| 12 | Minor | FrameGraph | `PassBuilder::read` and `write` are identical; no write hazard tracking | `FrameGraph.cpp:60–69` |
| 13 | Minor | Physics | `sweepCapsule` normal is always `-dir`; incorrect for sloped surfaces | `PhysicsWorld.cpp:574` |
| 14 | Minor | Physics | Trigger removal compares `.index` only, ignores generation | `PhysicsWorld.cpp:631` |
| 15 | Minor | FPS Camera | `eyeHeight` field declared but never applied to `transform.position.y` | `Camera.h:34` |
| 16 | Minor | FPS Camera | `FpsCameraSystem.h` duplicated in public and internal include paths | `FpsCameraSystem.h` (both) |
| 17 | Minor | Networking | `SnapshotAck` struct declared but never encoded/sent | `Snapshot.h:40` |
| 18 | Minor | Asset Pipeline | `EassHeader` and related structs duplicated in `EassetLoader.cpp` and `AssetImporter.cpp` | Both files |
| 19 | Minor | FPSGame | `SceneManager::load` returns `nullptr` for both "already loaded" and "file not found" | `SceneManager.cpp:24` |
| 20 | Minor | FPSGame | `startScenePath` hardcoded in `WinMain`, duplicates `project.toml` value | `FpsGameMain.cpp:17` |
| 21 | Minor | Lighting | Directional light uses `+Z` forward; camera uses `-Z`; conventions diverge without documentation | `LightCullSystem.cpp:62` |

---

*Report generated: 2026-06-08*
