# Phase 8 — Editor Maturity & Content Authoring

**Engine Version 0.8.x**
**Status:** Planning
**Prerequisites:** Phases 1–7 complete (confirmed 2026-06-04)

---

## Overview

Phase 7 delivered a working editor skeleton, a physics engine, scene serialization infrastructure, and the full FPS gameplay loop. What it left behind was authoring friction: you cannot import a model from the editor, you cannot see or edit a collider visually, there is no defined path for adding a new component, there is no character you can drop into a scene and play, and although `SceneSerializer` exists the editor has no File menu wired to it.

Phase 8 closes that gap. Every task here is about making the editor a tool a content author or junior programmer can actually use without reading engine source.

---

## Task List

| # | Title | Primary Module | Depends On |
|---|-------|---------------|------------|
| 60 | [Model Importing in the Editor](task-60-model-importing.md) | tools / editor | 58 (editor shell) |
| 61 | [Collision Geometry Editor](task-61-collision-geometry-editor.md) | physics / editor | 52 (physics), 58 |
| 62 | [New Component Workflow](task-62-component-workflow.md) | core / docs | 51, 58 |
| 63 | [Premade Character Entity](task-63-character-entity.md) | core / app | 50, 51, 52 |
| 64 | [Scene Save & Load in the Editor](task-64-scene-save-load.md) | tools / editor | 54 (SceneSerializer), 58 |
| 65 | [Entity Prefab System](task-65-prefab-system.md) | tools / editor | 51, 54, 64 |

**Implementation order:** 60 and 61 can start in parallel (no shared files). 62 is documentation only and can be written at any time. 63 depends on 61 (collider shapes need editor visibility before building the character). 64 must precede 65 (prefab workflow assumes a working scene file pipeline).

---

## What Is Not In Phase 8

- Animation state machine (stub fields exist on the character entity; full blending is Phase 9)
- Navmesh baking (field exists in `SceneGlobals`, baking tool is Phase 9)
- LOD system (import pipeline generates a single LOD; multi-LOD is Phase 9)
- Multiplayer testing inside PIE (PIE uses in-process local pair; real multi-client is Phase 9)
- Lag-compensation rewind (placeholder exists from Phase 7; implementation is Phase 9)

---

## Technical Debt

See [technical-debt.md](technical-debt.md) for a catalogued list of known issues carried forward from Phase 7, with severity ratings and suggested resolution phases.
