# Phase 9 — Playable Level & Release Readiness

**Engine Version 0.9.x**
**Status:** Planned (2026-06-05)
**Prerequisites:** Phases 1–8 complete, build clean, 386/386 unit tests pass

---

## Overview

Phase 8 delivered a content-authoring editor with working import, scene save/load, prefabs, and collision editing. What it left behind was the ability to actually *see* the scene you are building, *play* it with working physics and input, and *ship* it as a game. Phase 9 closes that gap.

The three pillars:

1. **Visual level authoring** — a working 3D viewport so level designers can see geometry, spawn points, and trigger volumes while they build. This is task #66 (camera) + #67 (viewport rendering), the prerequisite for everything else.
2. **Playable PIE** — spawn points (#68), physics and input wired into PIE (#69), and collision generated from imported meshes (#70) so pressing Play actually runs a game.
3. **Release build** — an `IGame` implementation and a wired `WinMain` so the engine produces a shippable game executable (#74).

The remaining tasks (#71–#79) address editor QoL, trigger volumes, and outstanding technical debt that must be resolved before multiplayer beta.

---

## Task List

| # | Title | Primary Module | Priority | Depends On |
|---|-------|---------------|----------|------------|
| [66](task-66-camera-integration.md) | Camera System Integration | app | critical path | — |
| [67](task-67-viewport-rendering.md) | Editor Viewport 3D Rendering | editor / rendering | critical path | #66 |
| [68](task-68-spawn-system.md) | SpawnPoint System | core / app / editor | critical path | — (#67 for icons) |
| [69](task-69-pie-physics-input.md) | PIE Physics + Input | editor | critical path | #68 |
| [70](task-70-collision-import.md) | Collision from Mesh Import | tools / editor | high | — |
| [71](task-71-authoring-tools.md) | Entity Authoring Tools | editor | high | — |
| [72](task-72-trigger-volumes.md) | Trigger Volume System | core / physics / editor | high | — (#67 for gizmos) |
| [73](task-73-lag-comp-rewind.md) | TD-03: Full Lag Compensation Rewind | physics / app | 🔴 critical debt | — |
| [74](task-74-igame-bootstrap.md) | IGame Bootstrap + Release Wiring | app | high | #68, #66 |
| [75](task-75-archetype-roundtrip.md) | TD-07: EntityFactory Archetype Round-Trip | tools / core | 🟠 high debt | — |
| [76](task-76-solver-threading.md) | TD-06: Physics Solver Threading | physics | 🟠 high debt | — |
| [77](task-77-navmesh-decision.md) | TD-04: Navmesh Field Decision | tools / core | 🟠 high debt | — |
| [78](task-78-registry-autodiscovery.md) | TD-13: ComponentEditorRegistry Auto-Discovery | editor | 🟡 medium debt | #74 |
| [79](task-79-rpc-validation.md) | TD-14: ENGINE_RPC Compile-Time Validation | networking | 🟡 medium debt | — |

---

## Dependency Graph

```
#66 (camera integration)
    └─► #67 (viewport rendering)
            ├─► icons for #68 (spawn gizmos)
            └─► gizmos for #72 (trigger volumes)

#68 (spawn system) ───────────────────────────���──────────┐
    └─► #69 (PIE physics + input)                        │
                                                          ▼
#66 + #68 ──────────────────��───────────────► #74 (IGame bootstrap)

#70 (collision import)   — independent
#71 (authoring tools)    — independent
#73 (lag-comp rewind)    — independent
#75 (archetype RT)       — independent
#76 (solver threading)   — independent
#77 (navmesh decision)   — independent
#78 (registry assert)    — after #74 stabilises component set
#79 (RPC validation)     — independent
```

**Recommended implementation order:**

1. **Wave 1 (unblock everything):** #66, #73 in parallel — #66 unblocks visual work, #73 fixes the highest-severity debt independently.
2. **Wave 2 (core gameplay):** #67, #68, #70 in parallel once #66 lands.
3. **Wave 3 (PIE + release):** #69, #71, #72, #74 in parallel once #67 + #68 land.
4. **Wave 4 (debt + hardening):** #75, #76, #77, #78, #79.

---

## New Component IDs

| ID | Component | Header | Task |
|----|-----------|--------|------|
| 14 | `core::SpawnPointComponent` | `src/core/public/core/components/SpawnPointComponent.h` | #68 |
| 15 | `core::TriggerComponent` | `src/core/public/core/components/TriggerComponent.h` | #72 |

Both must be registered in `Engine::init()` after `PrefabInstance` (ID 13) in declaration order.

---

## What Is Not In Phase 9

- **Animation state machine** — `AnimationState` stub fields exist; blending, blend trees, and state transitions are Phase 10.
- **Navmesh runtime queries / bot AI** — #77 resolves the dead-field issue but full pathfinding is Phase 10.
- **Nested prefabs / prefab auto-update** — Phase 9 prefab system is flat; nested and propagating prefabs are Phase 10.
- **LOD generation** — import pipeline produces one LOD; meshoptimizer-based LOD reduction is Phase 10.
- **Real multiplayer testing** — PIE uses in-process local pair; dedicated-server testing with real network lag is Phase 10.
- **Anti-cheat / competitive integrity** — lag-comp rewind (#73) improves fairness; full cheat detection is out of scope.

---

## Technical Debt Status

See [risks-and-architecture.md](risks-and-architecture.md) for cross-cutting architectural issues identified during Phase 9 planning.

### TD register as of 2026-06-05

| ID | Status | Resolution |
|----|--------|------------|
| TD-01 | ✅ Fixed | Server-side pitch clamp in DamageSystem |
| TD-02 | ✅ Fixed | Dedup expiry 640-tick window |
| TD-03 | ❌ Open | → #73 |
| TD-04 | ⚠️ Partial | LOG_WARN added; field decision → #77 |
| TD-05 | ✅ Fixed | BVH::remove() on entity destruction |
| TD-06 | ❌ Open | → #76 |
| TD-07 | ❌ Open | → #75 |
| TD-08 | ✅ Fixed | GameModeStateBlob static_assert |
| TD-09–12 | ✅ Fixed | Phase 8 tasks #61, #64 |
| TD-13 | ❌ Open | → #78 |
| TD-14 | ❌ Open | → #79 |
| TD-15 | ✅ Fixed | .meta sidecar in Phase 8 #60 |
| TD-16–20 | ✅ Fixed | Session 2026-06-05 |
