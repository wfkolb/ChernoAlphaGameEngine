# Phase 6 — Game Developer Documentation

**Status:** In progress (2026-04-27)
**Type:** Documentation only — no code changes this phase.
**Engine focus:** First-person shooter (FPS) game development.

---

## Goal

Produce the reference documentation a game developer needs to build an FPS game on this engine. Phase 6 covers the full game development workflow: project setup, entity creation, scene building, input, physics, networking/replication, saving, and the editor.

---

## Team Assignments This Phase

| Lead | Documents |
|---|---|
| **Project Lead** | Developer getting-started guide, input system, gameplay loop, save system |
| **Physics+Scene Lead** *(new)* | Entity system, scene system, physics engine design, scene serialization |
| **Networking Lead** | Gameplay object replication (co-authored with Physics+Scene Lead) |
| **Tools Lead** | Editor features and workflow |
| **Rendering Lead** | *No work this phase — rendering remains constant* |

---

## Documents

| File | Owner | Description |
|---|---|---|
| [task-01-developer-guide.md](task-01-developer-guide.md) | Project Lead | Getting started: project setup, IGame interface, ECS registration, FPS bootstrapping |
| [task-02-input-system.md](task-02-input-system.md) | Project Lead | InputAction, InputBinding, InputReceiver, networked input frames |
| [task-03-entity-system.md](task-03-entity-system.md) | Physics+Scene Lead | Entity/component model, FPS archetypes, EntityFactory, lifecycle |
| [task-04-scene-building.md](task-04-scene-building.md) | Physics+Scene Lead | Scene container, static/dynamic entities, level geometry, spatial queries |
| [task-05-physics-engine.md](task-05-physics-engine.md) | Physics+Scene Lead | Physics architecture, collision shapes, character controller, raycasting |
| [task-06-scene-serialization.md](task-06-scene-serialization.md) | Physics+Scene Lead | .scene file format, component serialization, runtime loading, versioning |
| [task-07-gameplay-loop.md](task-07-gameplay-loop.md) | Project Lead | Server-authoritative 64-tick loop, WorldState snapshot, client prediction |
| [task-08-save-system.md](task-08-save-system.md) | Project Lead | Player profiles, match records, server checkpoints, reconnection |
| [task-09-replication.md](task-09-replication.md) | Networking Lead + Physics+Scene Lead | NetworkIdentity, snapshot replication, prediction, interpolation, RPCs |
| [task-10-editor.md](task-10-editor.md) | Tools Lead | Editor layout, scene management, entity tools, level geometry, PIE |

---

## Key Design Decisions for FPS

- **Server authoritative, 64 Hz tick rate.** All gameplay state is owned by the server. Clients predict locally and reconcile with server snapshots.
- **Client-side prediction for local player movement.** The CharacterController applies input immediately on the client; the server corrects divergences > 5 cm.
- **Remote entity interpolation.** Non-local entities are rendered ~100 ms in the past using a 3-snapshot buffer for smooth movement without prediction artifacts.
- **Lag compensation for hit detection.** The server rewinds entity positions to the shooter's estimated timestamp before validating hit-scan results.
- **Physics runs on the server only.** Dynamic rigid bodies are authoritative on the server; their transforms are replicated to clients in every snapshot.
- **ECS archetype model.** Entities are defined by their component sets. FPS archetypes (Player, Weapon, Projectile, StaticProp, SpawnPoint, Trigger) are pre-defined; game devs add custom archetypes by registering component sets.

---

## Cross-Cutting Concerns

- **Input → Server:** Input frames `{tick, moveDir, yaw, pitch, buttons}` are sent to the server every tick. The server applies them authoritatively.
- **Scene ↔ Editor ↔ Runtime:** `.scene` files are written by the editor and read by the runtime. Same binary format; editor adds metadata overlays.
- **Replication ↔ Physics:** Physics state (position, velocity, on-ground) is part of the WorldState snapshot. Physics+Scene and Networking leads co-own the replication section for physics components.
- **Save ↔ Networking:** Checkpoint saves capture the server's authoritative WorldState. Reconnecting clients receive the current WorldState snapshot directly over the network — not from the checkpoint file.
