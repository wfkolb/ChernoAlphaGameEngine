# Task #61 — Collision Geometry Editor

**Phase 8 — physics / editor — Version 0.8.x**
**Audience:** Physics Lead, Editor developer
**Depends on:** Task #52 (PhysicsWorld, ColliderShape), Task #58 (EngineEditor)
**Resolves:** TD-09, TD-11, TD-12

---

## Table of Contents

1. [Goal](#1-goal)
2. [Current State](#2-current-state)
3. [Collider Inspector Widget](#3-collider-inspector-widget)
4. [Viewport Overlay — Wireframe Colliders](#4-viewport-overlay--wireframe-colliders)
5. [Collider Gizmos — Resize Handles](#5-collider-gizmos--resize-handles)
6. [Compound Colliders](#6-compound-colliders)
7. [Physics Material Editor Panel](#7-physics-material-editor-panel)
8. [Collision Layer Matrix Panel](#8-collision-layer-matrix-panel)
9. [New Files](#9-new-files)
10. [Undo/Redo Integration](#10-undoredo-integration)
11. [Tests](#11-tests)

---

## 1. Goal

A content author can, entirely within the editor:

- Add or remove a `Collider` component to any entity
- Choose a shape (Box / Sphere / Capsule / ConvexHull / TriangleMesh) and edit its parameters
- See the collider drawn in the Viewport as a wireframe overlay
- Drag handles in the Viewport to resize a Box or Sphere without typing numbers
- Edit physics materials via a dedicated panel (no TOML text editing required)
- Configure the 16-layer collision matrix via a visual grid panel

---

## 2. Current State

- `ColliderShape.h` defines all five shape types plus the `Collider` struct (shape variant + offset + layer + material + isTrigger)
- `InspectorPanel` has a `ComponentEditorRegistry` but no widget is registered for `Collider`
- `ViewportPanel` renders the scene but draws no debug geometry for physics shapes
- `PhysicsMaterialTable` reads from `config/physics_materials.toml` at startup; no reload path
- `QueryFilter` is a 16-layer bitmask matrix; no editor representation

---

## 3. Collider Inspector Widget

Register via `ComponentEditorRegistry::registerWidget<Collider>()`. The widget lives in `src/editor/component_widgets/ColliderWidget.h/.cpp`.

### 3.1 Shape Selector

```
[Shape]   ○ Box  ○ Sphere  ○ Capsule  ○ ConvexHull  ○ TriangleMesh
```

Changing shape type resets parameters to defaults. Prompt the user with "Change shape? This will reset shape parameters." before switching when parameters have been edited.

### 3.2 Per-Shape Parameter Fields

**Box:**
```
Half Extents   [X: 0.50]  [Y: 0.50]  [Z: 0.50]   (float drag, min 0.001)
```

**Sphere:**
```
Radius         [0.50]                              (float drag, min 0.001)
```

**Capsule:**
```
Radius         [0.25]
Half Height    [0.50]                              (total height = halfHeight * 2 + radius * 2)
```

**ConvexHull:**
```
[From Mesh ▼]  hero.easset                         (drop target for .easset)
Vertex count:  48  (read-only, shown after mesh assigned)
[Recalculate]                                      (re-runs QuickHull on new mesh)
```

**TriangleMesh:**
```
[From Mesh ▼]  terrain.easset
Triangle count: 4200  (read-only)
WARNING: TriangleMesh colliders are Static only.   (shown if entity has Dynamic RigidBody)
```

### 3.3 Common Fields (all shapes)

```
Local Offset   [X: 0.00]  [Y: 0.00]  [Z: 0.00]
Layer          [combo: Default / Player / Projectile / Trigger / ...]
Material       [combo: Concrete / Wood / Metal / ...]
Is Trigger     [checkbox]
```

### 3.4 Add / Remove Collider

In the InspectorPanel header for `Collider`, show a `[×]` remove button. The **Add Component** dropdown at the bottom of the Inspector lists `Collider` as an addable component (default shape: Box, halfExtents = 0.5 m).

---

## 4. Viewport Overlay — Wireframe Colliders

Draw collider geometry on top of the scene using the DX12 debug draw system (additive wireframe, no depth write).

### 4.1 Color Coding

| State | Color |
|-------|-------|
| Static (isTrigger = false) | Green `#00FF00` at 60% opacity |
| Dynamic / Kinematic | Cyan `#00FFFF` at 60% opacity |
| Trigger volume | Yellow `#FFFF00` at 60% opacity |
| Selected entity | White `#FFFFFF` at 100% opacity |

### 4.2 Shape Tessellation

| Shape | Wire primitive |
|-------|---------------|
| Box | 12 edges of a unit cube, scaled by halfExtents |
| Sphere | 3 great circles (XY, YZ, XZ planes), 32 segments each |
| Capsule | 2 hemispheres + 4 vertical lines, 16 segments |
| ConvexHull | Convex hull edges (compute once, cache) |
| TriangleMesh | Triangle wireframe (expensive — throttle to selected entity only) |

### 4.3 Toggle

A toolbar button in `ViewportPanel`: **[Physics]** (keyboard shortcut: `P`). When off, no collider wireframes are drawn (default: on in editor mode, always off in PIE).

### 4.4 Implementation Notes

Use a simple `DebugDraw` helper (`src/editor/DebugDraw.h/.cpp`) that batches line segments into a `D3D12_PRIMITIVE_TOPOLOGY_LINELIST` draw call. The buffer is rebuilt every frame from `Scene::forEachCollider()`. Cap at 50,000 line segments to prevent runaway debug geometry from huge TriangleMesh scenes.

---

## 5. Collider Gizmos — Resize Handles

When a `Collider` is selected in the Inspector and the **Collider tool** is active (keyboard: `C`, shown as a box icon in the viewport toolbar between scale and the select tool), draw resize handles.

### 5.1 Box Handles

6 face-center handles (±X, ±Y, ±Z). Dragging a handle extends or shrinks the corresponding half-extent. Hold `Alt` to resize symmetrically (both opposite faces move together).

### 5.2 Sphere Handle

1 handle on the +X axis at radius distance. Dragging it scales the radius uniformly.

### 5.3 Capsule Handles

2 handles: one at the top cap (+Y), one at the bottom cap (−Y) for halfHeight; one at the equator (+X) for radius.

### 5.4 ConvexHull / TriangleMesh

No resize handles — these shapes are driven by mesh data. Show a read-only bounding box instead.

### 5.5 Offset Handle

In all shape modes, a center translate handle (like ImGuizmo `TRANSLATE` but constrained to the entity's local space) moves `Collider.localOffset`. Snapping follows the same grid-snap setting as the entity transform gizmo.

---

## 6. Compound Colliders

An entity may have more than one `Collider` component. The ECS `World` supports multiple components of the same type on one entity (verify this — if not, introduce a `ColliderList` component that wraps `std::vector<Collider>` with `kComponentId = 9`).

In the Inspector, each collider is shown as a numbered, collapsible section:

```
▼ Collider [0]   Box   [×]
    Half Extents  ...
▼ Collider [1]   Sphere (Trigger)   [×]
    Radius  ...
[+ Add Collider]
```

`[+ Add Collider]` appends a new default Box to the list. `[×]` removes that index and pushes a `ColliderRemoveCommand` onto the undo stack.

---

## 7. Physics Material Editor Panel

**New panel:** `src/editor/panels/PhysicsMaterialsPanel.h/.cpp`

Add to the editor menu: **Window → Physics Materials**.

### 7.1 Table Layout

| # | Name | Static Friction | Dynamic Friction | Restitution | Sound Surface |
|---|------|----------------|-----------------|-------------|---------------|
| 0 | Concrete | 0.70 | 0.60 | 0.10 | Concrete |
| 1 | Wood | 0.55 | 0.45 | 0.20 | Wood |
| … | … | … | … | … | … |

All cells are editable (float drag for numbers, combo for Sound Surface).

**[+ Add Material]** appends a row. **[−]** removes the selected row (prompts if any collider references that index).

### 7.2 Save / Hot-Reload

**[Save]** writes `config/physics_materials.toml` and calls `PhysicsMaterialTable::reload()`. Changes take effect immediately in the editor scene and in any running PIE session.

No auto-save — changes are only committed when the user clicks **[Save]**.

---

## 8. Collision Layer Matrix Panel

**New panel:** `src/editor/panels/CollisionLayerPanel.h/.cpp`

Add to the editor menu: **Window → Collision Layers**.

### 8.1 Layer Names

Top section: 16 text input fields, one per layer, editable:
```
Layer  0: Default
Layer  1: Player
Layer  2: Projectile
Layer  3: Trigger
Layer  4: Static
…
Layer 15: (unused)
```

### 8.2 Collision Matrix

16×16 symmetric checkbox grid. Checking (i, j) means objects on layer i collide with objects on layer j. The matrix is symmetric — toggling (i, j) also toggles (j, i). The diagonal (i, i) controls self-collision within a layer.

```
         Default  Player  Projectile  Trigger  Static  …
Default    [✓]     [✓]      [✓]        [ ]      [✓]
Player            [✓]      [✓]        [✓]      [✓]
Projectile                 [✓]        [ ]      [✓]
…
```

### 8.3 Persistence

Layer names and matrix are stored in `config/collision_layers.toml`:

```toml
[layers]
names = ["Default", "Player", "Projectile", "Trigger", "Static", ...]

[matrix]
# Row per layer; bit i of row[j] = layer j collides with layer i
rows = [0b1111111111111111, 0b1111111111111111, ...]
```

`QueryFilter` is extended with a `QueryFilter::loadFromToml(path)` static method; called at `Engine::init()` and on **[Save]** in the panel.

---

## 9. New Files

```
src/editor/
├── DebugDraw.h / .cpp                         — batched line-list debug renderer
├── component_widgets/
│   └── ColliderWidget.h / .cpp               — Collider inspector widget
└── panels/
    ├── PhysicsMaterialsPanel.h / .cpp        — material editor
    └── CollisionLayerPanel.h / .cpp          — layer matrix editor
```

### Modify Existing Files

- `src/editor/panels/InspectorPanel.cpp` — register `ColliderWidget` at startup
- `src/editor/panels/ViewportPanel.cpp` — call `DebugDraw` each frame when physics overlay is on; add Collider tool button to toolbar
- `src/editor/EditorApp.cpp` — add panel instances; add menu items
- `src/physics/public/physics/QueryFilter.h` — add `loadFromToml(path)` declaration
- `src/physics/QueryFilter.cpp` — implement `loadFromToml`

---

## 10. Undo/Redo Integration

All collider edits go through `ICommand`:

| User action | Command class |
|-------------|---------------|
| Change shape type | `ColliderShapeChangeCommand` |
| Drag shape parameter (done) | `ColliderParamCommand` (coalesced while dragging) |
| Drag resize handle (done) | `ColliderResizeCommand` (coalesced while dragging) |
| Add collider | `ColliderAddCommand` |
| Remove collider | `ColliderRemoveCommand` |
| Change layer / material | `ColliderPropertyCommand` |

**Coalescing:** While the user is actively dragging a handle, intermediate states are merged into a single undo entry. The command is finalized on mouse-up (`ImGui::IsItemDeactivatedAfterEdit()`).

---

## 11. Tests

**File:** `tests/editor/ColliderWidgetTests.cpp` (label: unit)

- Round-trip: set Box halfExtents via `ColliderWidget`, undo, verify original values restored
- Layer matrix: toggle (1, 3), verify (3, 1) also toggled; save/load round-trip via `collision_layers.toml`
- Physics material: add material, save, reload `PhysicsMaterialTable`, verify index is accessible

GPU-dependent tests (wireframe rendering, gizmo picking) use `GTEST_SKIP` when `!device.isValid()`.
