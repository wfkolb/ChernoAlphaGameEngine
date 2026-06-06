# Task #63 — Premade Character Entity

**Phase 8 — core / app — Version 0.8.x**
**Audience:** Physics+Scene Lead, Gameplay programmer
**Depends on:** Task #50 (InputSystem), Task #51 (FPS components + EntityFactory), Task #52 (CharacterController)
**Blocks:** Nothing — but Phase 9 animation and weapon systems will extend this entity

---

## Table of Contents

1. [Goal](#1-goal)
2. [Design Overview](#2-design-overview)
3. [Component Layout](#3-component-layout)
4. [New Components Required](#4-new-components-required)
5. [FpsCharacterEntity Archetype](#5-fpschacterentity-archetype)
6. [Hierarchy Structure](#6-hierarchy-structure)
7. [Spawn Flow](#7-spawn-flow)
8. [Editor Integration](#8-editor-integration)
9. [Defaults and Tuning Values](#9-defaults-and-tuning-values)
10. [Tests](#10-tests)
11. [What Is Explicitly Not in Phase 8](#11-what-is-explicitly-not-in-phase-8)

---

## 1. Goal

A designer opens the editor, opens the Spawn Entity menu, selects **FpsCharacter**, clicks in the Viewport, and a fully configured character appears: capsule collider, camera arm, first-person view offset, health, team tag, input receiver, and network identity. The character can be placed in a scene, saved, and played in PIE using keyboard+mouse input.

This is not a visual-art-complete character — placeholder geometry is acceptable (a capsule mesh). The goal is a working, playable, inspector-editable entity that a gameplay programmer or animator can extend without touching C++ for the common cases.

---

## 2. Design Overview

The character is an **entity hierarchy** (not a monolithic flat entity):

```
FpsCharacter (root)
├── Components: Transform, CharacterController, Health, TeamTag,
│              InputReceiverComponent, NetworkIdentity, RigidBody (Kinematic),
│              Collider (Capsule), AnimationState (new, stub)
│
├── CameraArm (child entity)
│   └── Components: Transform (local offset from character eye height)
│
├── FirstPersonMesh (child entity)
│   └── Components: Transform, MeshHandle (arms/weapon — visible only to local player)
│
└── ThirdPersonMesh (child entity)
    └── Components: Transform, MeshHandle (full body — visible to other players, hidden for local)
```

Each child entity is a full ECS entity with a parent-child relationship stored via a `HierarchyComponent` (see Section 4).

---

## 3. Component Layout

### Root Entity Components

| Component | ID | Values | Notes |
|-----------|-----|--------|-------|
| `Transform` | 1 | position=params, rotation=params | Set at spawn |
| `CharacterController` | 7 | see Section 9 | Capsule dims, speed, jump |
| `Health` | 3 | maxHp=100, currentHp=100, shieldPercent=0 | Full health at spawn |
| `TeamTag` | 5 | teamId=0xFF (unassigned) | Server sets real team on join |
| `InputReceiverComponent` | 2 | priority=10, focusGroup=Gameplay | Higher priority than UI (0) |
| `NetworkIdentity` | 8 | ownerClientId=SERVER_OWNED initially | Server assigns owner on join |
| `RigidBody` | 6 | type=Kinematic | Physics body; CharacterController drives it |
| `Collider` | — | Capsule, radius=0.35m, halfHeight=0.525m | Layer=Player |
| `AnimationState` | 9* | see Section 4 | Stub; no blending in Phase 8 |

*AnimationState is a new component added in this task (ID 9, or next available after Task #61 additions).

### Child Entity Components

**CameraArm:**
| Component | Values |
|-----------|--------|
| `Transform` | localOffset = (0, 1.65, 0) — eye height 1.65 m above root |
| (no physics) | Camera arm is purely positional |

**FirstPersonMesh:**
| Component | Values |
|-----------|--------|
| `Transform` | localOffset = (0.15, -0.20, 0.35) — right-handed offset for arms |
| `MeshHandle` | `first_person_arms.easset` (placeholder capsule until art is provided) |

**ThirdPersonMesh:**
| Component | Values |
|-----------|--------|
| `Transform` | localOffset = (0, 0, 0) |
| `MeshHandle` | `third_person_capsule.easset` (placeholder capsule) |

---

## 4. New Components Required

### 4.1 `AnimationState` (stub)

`src/core/public/core/components/AnimationState.h`

```cpp
struct AnimationState {
    static constexpr ComponentTypeId kComponentId = 9;  // verify against table

    enum class Clip : uint8_t {
        Idle = 0,
        Walk,
        Run,
        Jump,
        Fall,
        Land,
        Count
    };

    Clip    currentClip      = Clip::Idle;
    Clip    previousClip     = Clip::Idle;
    float   clipTimeSeconds  = 0.f;   // playhead within current clip
    float   blendWeight      = 1.f;   // 0=previous, 1=current; full transition in Phase 9
    bool    isGrounded       = false; // mirrored from CharacterController for animator read
};
```

Phase 8 does not implement any animation blending. The struct exists so gameplay code and the editor can write to it and Phase 9 can wire in a real animator without changing the data layout.

### 4.2 `HierarchyComponent`

`src/core/public/core/ecs/HierarchyComponent.h`

Required to implement the entity tree. This is foundational and likely needs to be its own sub-task within #63.

```cpp
struct HierarchyComponent {
    static constexpr ComponentTypeId kComponentId = 10;  // verify

    EntityId parent   = kInvalidEntity;
    EntityId firstChild = kInvalidEntity;
    EntityId nextSibling = kInvalidEntity;
    EntityId prevSibling = kInvalidEntity;
};
```

The hierarchy is a linked list of siblings. `SceneHierarchyPanel` already expects this structure (it renders a tree) — this task makes it real. `Scene::tick(dt)` propagates world transforms from parent to child using a depth-first traversal.

**Note:** `HierarchyComponent` must be registered in `Engine::init()` before `AnimationState` to maintain ID ordering. Assign IDs carefully.

### 4.3 `MeshHandle` (if not already a component)

If the current system stores mesh references on the entity differently (e.g., via a `RenderMesh` component or just an index in the rendering module), audit the existing representation before adding another. The goal is a component that the InspectorPanel can render as an asset-drop target. If this already exists, use it.

---

## 5. FpsCharacterEntity Archetype

`src/core/fps/FpsArchetypes.cpp` — add to `registerFpsArchetypes()`:

```cpp
factory.registerArchetype("FpsCharacter", [](const SpawnParams& p, World& w) -> EntityId {
    // --- Root ---
    auto root = w.createEntity();
    w.addComponent<Transform>(root, Transform{ .position = p.position, .rotation = p.rotation });
    w.addComponent<CharacterController>(root, makeDefaultCharacterController());
    w.addComponent<Health>(root, Health{ .maxHp = 100, .currentHp = 100 });
    w.addComponent<TeamTag>(root, TeamTag{ .teamId = 0xFF });
    w.addComponent<InputReceiverComponent>(root, InputReceiverComponent{
        .priority = 10, .focusGroup = FocusGroup::Gameplay
    });
    w.addComponent<NetworkIdentity>(root, NetworkIdentity{
        .ownerClientId  = NetworkIdentity::SERVER_OWNED,
        .replicatedComponents = RCB_TRANSFORM | RCB_HEALTH | RCB_ANIMATION_STATE
    });
    w.addComponent<RigidBody>(root, RigidBody{ .type = RigidBodyType::Kinematic });
    w.addComponent<Collider>(root, makeCapsuleCollider(0.35f, 0.525f, PhysicsLayer::Player));
    w.addComponent<AnimationState>(root, AnimationState{});

    // --- CameraArm ---
    auto cameraArm = w.createEntity();
    w.addComponent<Transform>(cameraArm, Transform{ .position = {0.f, 1.65f, 0.f} });
    w.addComponent<HierarchyComponent>(cameraArm, HierarchyComponent{ .parent = root });
    linkChild(w, root, cameraArm);

    // --- FirstPersonMesh ---
    auto fpMesh = w.createEntity();
    w.addComponent<Transform>(fpMesh, Transform{ .position = {0.15f, -0.20f, 0.35f} });
    w.addComponent<MeshHandle>(fpMesh, MeshHandle{ .assetPath = "assets/first_person_arms.easset" });
    w.addComponent<HierarchyComponent>(fpMesh, HierarchyComponent{ .parent = cameraArm });
    linkChild(w, cameraArm, fpMesh);

    // --- ThirdPersonMesh ---
    auto tpMesh = w.createEntity();
    w.addComponent<Transform>(tpMesh, Transform{});
    w.addComponent<MeshHandle>(tpMesh, MeshHandle{ .assetPath = "assets/third_person_capsule.easset" });
    w.addComponent<HierarchyComponent>(tpMesh, HierarchyComponent{ .parent = root });
    linkChild(w, root, tpMesh);

    return root;
});
```

`makeDefaultCharacterController()`, `makeCapsuleCollider()`, and `linkChild()` are free helper functions defined in `FpsArchetypes.cpp` (not public API).

---

## 6. Hierarchy Structure

### 6.1 Transform Propagation

`Scene::tick(dt)` must propagate world transforms top-down after physics and CharacterController:

```
for each root entity (HierarchyComponent.parent == kInvalidEntity):
    worldTransform[root] = localTransform[root]
    propagateChildren(root)

propagateChildren(parent):
    for each child in siblings starting at HierarchyComponent.firstChild:
        worldTransform[child] = worldTransform[parent] * localTransform[child]
        propagateChildren(child)
```

This is a new traversal in `Scene::tick()`. Depth limit: 8 levels (enough for character hierarchy; `ENGINE_ASSERT` if exceeded).

### 6.2 SceneHierarchyPanel

Update `SceneHierarchyPanel` to use `HierarchyComponent` for tree rendering (it currently renders a flat list). Indent child entities; expand/collapse with arrow. Drag-and-drop to reparent entities (push a `ReparentCommand` onto the undo stack).

---

## 7. Spawn Flow

### Via Editor (Spawn Entity menu)

1. User selects **Spawn → FpsCharacter** from the editor toolbar or right-click in the Viewport
2. Editor enters "placement mode" — cursor shows a character silhouette
3. Left-click in the Viewport: raycast to find floor position; call `EntityFactory::spawn("FpsCharacter", params, scene.world())`
4. Selected entity becomes the spawned root; SceneHierarchyPanel expands to show child entities
5. Push `CreateEntityCommand` (wraps all 4 entity creations as one undoable operation)

### Via Code (game / server)

```cpp
SpawnParams p;
p.position = spawnPoint.position;
p.rotation = spawnPoint.rotation;
EntityId character = entityFactory.spawn("FpsCharacter", p, world);
// Assign ownership on server:
world.getComponent<NetworkIdentity>(character)->ownerClientId = clientId;
world.getComponent<TeamTag>(character)->teamId = assignedTeam;
```

---

## 8. Editor Integration

### 8.1 AnimationState Widget

`src/editor/component_widgets/AnimationStateWidget.h/.cpp`

Shows:
- Current clip name (read-only in editor, the runtime drives it)
- Clip time (read-only)
- Blend weight (editable for testing transitions manually)
- **[Force Clip]** combo box — override clip in PIE for animation testing

### 8.2 HierarchyComponent Widget

In the Inspector, show parent entity name (clickable → selects parent) and child count. Do not expose the raw linked-list pointers; they are implementation detail.

### 8.3 Placeholder Assets

Two placeholder `.easset` files must be included in the project (either checked in pre-cooked or generated as part of the build):
- `assets/first_person_arms.easset` — unit capsule, ~200 tris
- `assets/third_person_capsule.easset` — unit capsule, ~200 tris

These can be generated from embedded geometry in `asset_cooker` (add a `--generate-placeholder <name>` flag) or committed as binary files from the `tools/` directory.

---

## 9. Defaults and Tuning Values

| Parameter | Value | Source |
|-----------|-------|--------|
| Capsule radius | 0.35 m | Typical FPS character half-width |
| Capsule half-height | 0.525 m | Total standing height: 0.35×2 + 0.525×2 = 1.75 m |
| Eye height offset | 1.65 m | 1.65 / 1.75 = 94% of total height (realistic) |
| Max walk speed | 5.5 m/s | Standard FPS |
| Max run speed | 9.0 m/s | 1.6× walk |
| Jump impulse | 5.0 m/s initial vertical velocity | ~0.64 m apex at 9.8 m/s² gravity |
| Step-up height | 0.25 m | One stair step |
| Max slope angle | 46° | Slight above 45° to avoid stair-edge snapping |
| Coyote time | 0.12 s | 7–8 frames at 64 Hz |
| Jump buffer | 0.10 s | ~6 frames |
| Health | 100 HP | Standard |
| Shield | 0% | Unarmoured by default |

All values above are starting points. They live in `FpsArchetypes.cpp` inside `makeDefaultCharacterController()`, not hard-coded at call sites, so a single edit tunes all spawned characters.

---

## 10. Tests

**File:** `tests/core/FpsCharacterTests.cpp` (label: unit)

- Spawn `FpsCharacter` in a headless `World`; verify all expected components are present on the root and child entities
- Hierarchy traversal: verify `HierarchyComponent.parent` of child = root entity ID
- `AnimationState` defaults: `currentClip == Idle`, `clipTimeSeconds == 0.f`
- Health at spawn: `currentHp == maxHp == 100`
- `Collider` shape is Capsule with correct radius and halfHeight
- `RigidBody` type is Kinematic
- Undo/redo of spawning the character removes all 4 entities and restores them

**File:** `tests/core/HierarchyComponentTests.cpp` (label: unit)

- Attach parent–child–grandchild; verify linked-list links are consistent
- Remove middle entity; verify remaining chain is intact
- World-transform propagation: rotate parent 90°, verify child world position is rotated accordingly

---

## 11. What Is Explicitly Not in Phase 8

- **Animation blending** — `AnimationState.blendWeight` field exists but is unused; no animator update loop
- **IK (inverse kinematics)** — Phase 9 scope
- **Procedural footstep audio** — Phase 9 scope
- **Weapon attachment sockets** — The `FirstPersonMesh` child entity is a placeholder; weapon mounting is Phase 9
- **First-person / third-person mesh visibility switching** — Rendering visibility flags are Phase 9 (the meshes exist on the entities but are both visible to all cameras in Phase 8)
- **Crouch / prone** — `CharacterController` has no crouch state in Phase 7; adding it is Phase 9
