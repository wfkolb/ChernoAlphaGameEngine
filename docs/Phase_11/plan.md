# Phase 11 — Architecture Closure + FPS Game Integration

**Status:** Wave 1 ✅ (2026-06-10) — Wave 2 ✅ (2026-06-10) — Wave 3 ✅ (2026-06-10) — **COMPLETE**

---

## Wave Summary

### Wave 1 ✅ COMPLETE (2026-06-10)

| ID  | Title                                          | Notes |
|-----|------------------------------------------------|-------|
| E1  | Resolve `Renderable` vs `MeshHandle` confusion | Deleted `Renderable.h`; added `rendering::RenderableComponent` alias |
| N1  | `InputMessage` wire struct + GameLoop wiring   | `InputMessage.h`, `InputMessageSerializer`, `NetworkInputComponent` (id=19); ring buffer + `flushInputMessages`/`receiveInputMessage`; 11 tests |
| N2  | Wire `IGameMode` trigger event callbacks       | `GameLoop::Desc::eventBus`; `TriggerEnter/ExitEvent` subscriptions; 5 tests |
| N3  | SpawnPoint radius exclusion in `onPlayerJoin`  | Radius exclusion filter with fallback; 5 tests |
| C1  | `[render].shadowSplitLambda` config            | `ShadowPass.h/cpp`; `Config::getRenderShadowSplitLambda()`; `SceneGlobals::shadowSplitLambda`; 5 tests |

### Wave 2 ✅ COMPLETE (2026-06-10)

| ID  | Title                                              | Notes |
|-----|----------------------------------------------------|-------|
| F1  | FpsGame networked game loop (server + client mode) | `NetworkConfig`/`NetworkMode`; `GameLoop`+`Session` in `Impl`; server/client/standalone routing |
| F2  | FpsGame trigger callbacks + objective zones        | `TriggerComponent::tag[32]`; `objectiveProgress_`; `onTriggerEnter/Exit`; 3 trigger tests |
| F3  | FpsGame archetype + scene update for Renderable    | `FpsArchetypes.cpp` keeps `core::MeshHandle` directly (arch rule: `engine_core` ≠ `engine_rendering`) |
| FE1 | FPSGameEditor archetype and inspector updates      | `FPSGameEditorMain.cpp` includes `<rendering/RenderableComponent.h>` |
| FE2 | FPSGameEditor: skybox/shadow config in scene panel | `ScenePropertiesPanel` adds Skybox + Shadows sections |

**After Wave 2:** 552/552 engine unit tests (3 permanently skipped); 13/13 FPSGame tests.

### Wave 3 ✅ COMPLETE (2026-06-10)

| ID  | Title                             | Notes |
|-----|-----------------------------------|-------|
| F4  | FpsGame end-to-end networked demo | `project.toml` with `[network]` section; `assets/scenes/test_network.scene` (binary, Python generator); `README.md`; 3 integration tests (server+client 10-tick loop, smoke, flush) |

**After Wave 3:** 552/552 engine unit tests; 16/16 FPSGame tests (13 unit + 3 integration).

---

## F4 — FpsGame End-to-End Networked Demo

**Owner:** App Lead + Networking Lead

With F1–F3 complete, validate the full server+client loop works in a real game session.

**Deliverables:**

1. **`FPSGame/project.toml`** — sample config with `[network]` section documented (mode, port, serverAddress).

2. **`FPSGame/assets/scenes/test_network.scene`** — minimal demo scene with:
   - Two spawn points (one per team).
   - One trigger zone with `tag = "objective_a"`.
   - A directional shadow-casting light.
   - `skyboxAssetPath` pointing to a sample HDR `.easset` path (placeholder OK).

3. **`FPSGame/README.md`** — instructions to launch server + client and verify:
   1. Client connects and receives initial snapshot.
   2. Player moves (InputMessage flow).
   3. Trigger zone fires `onTriggerEnter` and logs.

4. **`FPSGame/tests/network/FpsGameIntegrationTest.cpp`** (label: `integration`) — in-process server+client using `Session::createLocalPair`; client sends 10 ticks of inputs; server snapshot received by client; verify entity positions converge within 1 cm. Add to `FPSGame/tests/CMakeLists.txt`.
