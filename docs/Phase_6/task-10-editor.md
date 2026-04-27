# Editor: Features, Workflow, and Design

**Phase 6 — Tools Lead Reference Document**
**Module:** `engine::tools` / `EngineEditor.exe`
**Status:** Planned — this document defines the editor's intended design, not a description of existing code.

---

## Table of Contents

1. [Editor Architecture](#1-editor-architecture)
2. [Main Editor Layout](#2-main-editor-layout)
3. [Scene Management](#3-scene-management)
4. [Scene Hierarchy Panel](#4-scene-hierarchy-panel)
5. [Inspector Panel](#5-inspector-panel)
6. [Viewport and Transform Gizmos](#6-viewport-and-transform-gizmos)
7. [Level Geometry Tools](#7-level-geometry-tools)
8. [Player Start Positions](#8-player-start-positions)
9. [Asset Browser](#9-asset-browser)
10. [Trigger Volumes](#10-trigger-volumes)
11. [Play-in-Editor (PIE)](#11-play-in-editor-pie)
12. [Undo/Redo System](#12-undoredo-system)
13. [Editor Project Settings](#13-editor-project-settings)

---

## 1. Editor Architecture

### 1.1 The Editor Executable

The editor ships as a standalone Windows executable, `EngineEditor.exe`, built from the `editor` CMake target. It links against every engine library module:

| Linked Library     | Purpose                                                    |
|--------------------|------------------------------------------------------------|
| `engine::core`     | ECS world, component registry, memory, math                |
| `engine::rendering`| DX12 backend, render graph, material system                |
| `engine::tools`    | AssetImporter (glTF→.easset), EassetLoader                 |
| `engine::physics`  | (Phase 7+) rigid bodies, collision, triggers               |

The editor is **not** a plugin or an in-game overlay. It is a first-class application that boots its own DX12 window, initializes its own ECS `World`, and drives the full engine tick loop. Game code is compiled as a separate shared library (`game.dll`) that the editor hot-reloads on disk change.

### 1.2 DX12 Window and Render Target Integration

The editor shares one DX12 swap chain with the game. ImGui is initialized against that same swap chain. The 3D viewport is an `ID3D12Resource` render target; its `ImTextureID` handle is passed to `ImGui::Image()` inside the Viewport panel. This means:

- There is no performance penalty for mirroring two D3D devices.
- The editor rendering and game rendering share the same resource heap allocator.
- The swap chain present happens once per frame after both the editor UI and the 3D scene have been recorded into the command list.

### 1.3 Edit Mode vs. Simulation Mode

The editor maintains two distinct world states:

**Edit Mode (default)**

- The ECS `World` holds the scene as loaded from the `.scene` file.
- No physics simulation runs; colliders are only visualized, not stepped.
- No networking stack is active.
- Entity transforms can be mutated freely by the editor gizmos and Inspector.
- This is the state the user modifies and saves.

**Play-in-Editor (PIE) Mode**

- The editor serializes the current edit-mode world into a temporary `.scene` buffer in memory.
- A local server instance and a local client instance are spawned in-process on separate threads.
- The physics simulation begins stepping at the configured fixed timestep.
- The editor camera is replaced by the player's first-person camera via the normal `CameraSystem`.
- On Stop, the server and client are torn down, the in-memory temp buffer is discarded, and the edit-mode world is restored from the original on-disk scene file.

The separation is intentional: PIE changes **never** mutate the saved scene. The designer can freely test without fear of corrupting level data.

### 1.4 Dear ImGui (Docking Branch)

All editor UI is built with Dear ImGui from the docking branch (`imgui_docking`). Key details:

- `ImGuiConfigFlags_DockingEnable` is set on startup.
- A full-screen invisible DockSpace covers the main window, allowing panels to be freely rearranged.
- Each panel is an `ImGui::Begin` / `ImGui::End` block with `ImGuiWindowFlags_NoCollapse` to prevent accidental collapse.
- The default layout is serialized to `editor_layout.ini` in the project directory; users can reset to the default via **View → Reset Layout**.
- ImPlot and ImGuizmo are additional Dear ImGui extensions bundled with the editor.

---

## 2. Main Editor Layout

The default panel arrangement follows a standard level-editor convention familiar to designers coming from Unreal or Unity.

```
┌─────────────────────────────────────────────────────────────────┐
│  Toolbar (top — Play, Stop, Save, Build, Settings...)           │
├──────────────┬──────────────────────────────┬───────────────────┤
│              │                              │                   │
│  Scene       │       3D Viewport            │   Inspector       │
│  Hierarchy   │   (DX12 render target)       │   (right)         │
│  (left)      │                              │                   │
│              │                              │                   │
├──────────────┴──────────────────────────────┴───────────────────┤
│  Asset Browser (bottom-left)   │   Console / Log (bottom-right) │
└────────────────────────────────┴────────────────────────────────┘
```

| Panel            | Default Dock Position | Description                                          |
|------------------|-----------------------|------------------------------------------------------|
| Toolbar          | Top strip             | Global actions: Play/Stop, scene save, build         |
| Scene Hierarchy  | Left column           | Tree of all entities in the current scene            |
| 3D Viewport      | Center                | Rendered scene; gizmos; camera navigation            |
| Inspector        | Right column          | Component details for the selected entity            |
| Asset Browser    | Bottom-left           | Project asset directory browser                      |
| Console / Log    | Bottom-right          | Engine log, editor warnings, script output           |

All panels are dockable `ImGui` windows. Users can detach any panel, float it, or re-dock it. The layout is persisted in `editor_layout.ini`. Additional panels (Level Settings, Mesh Preview, Material Editor) open as floating windows by default.

---

## 3. Scene Management

### 3.1 New Scene

**Menu: File → New Scene** (shortcut: `Ctrl+N`)

1. Editor prompts "Save changes to current scene?" if the edit-mode world has unsaved mutations.
2. A new, empty ECS `World` is created.
3. Default scene globals are set: gravity `(0, -9.81, 0)`, ambient color `(0.1, 0.1, 0.1)`, no default physics material.
4. The Scene Hierarchy panel clears. The Viewport shows an empty world with the grid overlay.
5. The scene is considered "untitled" until first save.

### 3.2 Open Scene

**Menu: File → Open Scene** (shortcut: `Ctrl+O`)

1. A Win32 `GetOpenFileName` dialog filters for `*.scene`.
2. The selected `.scene` file is deserialized by the `SceneLoader` (owned by the Physics+Scene lead).
3. The edit-mode ECS `World` is populated with the loaded entities and components.
4. The Scene Hierarchy panel is rebuilt.
5. The path is added to the Recent Scenes list (capped at 10 entries, stored in `editor_prefs.toml`).

### 3.3 Save Scene / Save As

**Menu: File → Save Scene** (`Ctrl+S`) / **File → Save Scene As** (`Ctrl+Shift+S`)

- Save serializes the current edit-mode `World` back to the original `.scene` file path using `SceneSerializer`.
- Save As opens a Win32 `GetSaveFileName` dialog, writes to the chosen path, and updates the window title.
- Save is **not** undoable. The undo stack is NOT cleared on save (the user can still undo past a save to test, but the on-disk file reflects the state at the time of save).

### 3.4 Recent Scenes List

**Menu: File → Recent Scenes** shows the last 10 opened/saved `.scene` paths, most recent first. Clicking a path opens that scene (with the same unsaved-changes prompt as Open Scene). Paths that no longer exist on disk are shown greyed out and removed on next editor launch.

### 3.5 Scene Settings Panel

**Menu: Scene → Scene Settings** opens a floating panel with the following fields:

| Field                   | Type           | Description                                        |
|-------------------------|----------------|----------------------------------------------------|
| Scene Name              | string         | Display name; stored in `.scene` header            |
| Gravity Vector          | vec3 input     | World gravity, default `(0, -9.81, 0)`             |
| Ambient Color           | color picker   | Placeholder; forwarded to the rendering team       |
| Default Physics Material| asset picker   | `.physics_material` asset applied when none specified |

Changes in the Scene Settings panel are undoable (wrapped in a `SceneGlobalsChangeCommand`).

---

## 4. Scene Hierarchy Panel

The Scene Hierarchy panel is the primary structural view of the scene. It mirrors the ECS entity list and reflects parent-child relationships encoded in the `HierarchyComponent`.

### 4.1 Entity Display

- Each entity is listed by its `NameComponent::name` string.
- If an entity has no `NameComponent`, the fallback label is `Entity #<id>` where `<id>` is the `EntityId` integer.
- Parent entities render as collapsible tree nodes; child entities are indented beneath them.
- The expand/collapse state is local editor UI state and is not persisted to the `.scene` file.

### 4.2 Selection

- **Single-click** selects an entity; the Inspector panel updates to show its components.
- **Ctrl+click** adds to the current selection (multi-select).
- **Shift+click** range-selects all entities between the last selected and the clicked item.
- The selected entity is highlighted in the Viewport with a bounding-box outline.
- Selection is bidirectional: clicking an entity in the Viewport selects it in the Hierarchy, and vice versa.

### 4.3 Bulk Operations (Multi-select)

When multiple entities are selected:

- **Delete**: removes all selected entities (one undoable `BulkDeleteCommand`).
- **Move via gizmo**: translates all selected entities, maintaining relative offsets.
- **Group**: creates a new parent entity (named "Group") and re-parents all selected entities under it.

### 4.4 Right-Click Context Menu

Right-clicking any entity in the hierarchy shows:

| Menu Item        | Action                                                                 |
|------------------|------------------------------------------------------------------------|
| Rename           | Opens an inline text input field                                       |
| Duplicate        | Creates a deep copy of the entity and its children                     |
| Delete           | Removes the entity and its descendants (undoable)                      |
| Create Child     | Creates a new empty entity parented to this entity                     |
| Add Component    | Opens the Add Component searchable dropdown (same as Inspector button) |
| Unparent         | Moves the entity to the scene root                                     |

### 4.5 Search and Filter

A search bar at the top of the Hierarchy panel filters entities by name in real time. The filter is case-insensitive and matches substrings. Entities that do not match (but are ancestors of matching entities) are shown in a dimmed state to preserve tree context. Clear the filter with the `X` button or `Escape`.

---

## 5. Inspector Panel

The Inspector shows the components attached to the currently selected entity. When multiple entities are selected, the Inspector shows only components that are **present on all selected entities** (intersection), allowing bulk editing of shared properties.

### 5.1 Component Widgets

Each component type has a registered editor widget function. The built-in widgets are:

| Component          | Widget Description                                                           |
|--------------------|------------------------------------------------------------------------------|
| `TransformComponent` | Three vec3 inputs: Position, Rotation (Euler degrees), Scale               |
| `MeshComponent`    | Asset picker showing the current `.easset` path; drag-and-drop from browser |
| `BoxColliderComponent` | Three float inputs for half-extents X/Y/Z                               |
| `SphereColliderComponent` | Single float input for radius                                        |
| `CapsuleColliderComponent` | Float inputs for radius and half-height                             |
| `HealthComponent`  | Int input for MaxHP; read-only current HP display during PIE               |
| `NameComponent`    | Single text input                                                            |
| `TeamTagComponent` | Dropdown: Team A / Team B / Any                                              |
| `TriggerComponent` | Shape selector; callback name text inputs (OnEnter, OnStay, OnExit)        |

### 5.2 Registering Custom Component Widgets

Game code can register editor widgets for any component type using `ComponentEditorRegistry`:

```cpp
// In game initialization code (called before editor UI first renders):
ComponentEditorRegistry::registerWidget<MyHealthPickupComponent>(
    [](MyHealthPickupComponent& comp, EntityId id) {
        ImGui::SliderFloat("Heal Amount", &comp.healAmount, 0.0f, 100.0f);
        ImGui::Checkbox("Respawns", &comp.respawns);
        if (comp.respawns) {
            ImGui::DragFloat("Respawn Delay (s)", &comp.respawnDelay, 0.1f, 0.0f, 60.0f);
        }
    }
);
```

The lambda receives a mutable reference to the component and the owning `EntityId`. Any value change within the lambda is automatically wrapped in an `InspectorChangeCommand` and pushed to the undo stack by the editor framework. Custom widgets are registered at game DLL load and unregistered on DLL unload, enabling hot-reload compatibility.

### 5.3 Add / Remove Component

- **Add Component**: A button at the bottom of the Inspector opens a searchable dropdown populated from the `ComponentRegistry`. Selecting a component type adds it to the entity with default-constructed values (undoable).
- **Remove Component**: Each component header has a small `[-]` button on the right. Clicking it removes the component (undoable). Removing a component that other components depend on (e.g., removing `TransformComponent` while a `MeshComponent` is present) shows a confirmation dialog listing dependent components.

### 5.4 Modified Highlighting and Undo/Redo

- Fields whose values differ from the last-saved `.scene` state are shown with a yellow left border.
- **Ctrl+Z** undoes the last inspector change; **Ctrl+Y** redoes it.
- The undo stack is shared across all editor panels (Hierarchy renames, Inspector value changes, Viewport gizmo moves all share one stack).

---

## 6. Viewport and Transform Gizmos

### 6.1 Rendering

The 3D Viewport renders the scene through the engine's standard DX12 render graph: geometry pass, lighting pass, post-process pass. The output is resolved to an `ImTextureID` and displayed via `ImGui::Image()`. The viewport texture is resized when the ImGui window is resized, triggering a render target recreate on the next frame.

In Edit Mode, the scene is rendered with:
- All opaque and alpha-tested meshes using the same shaders as the game.
- Editor-only overlays drawn in a separate debug draw pass (grid, gizmos, selection outlines, collider wireframes).
- No post-process effects that would interfere with overlay readability (FXAA is disabled in Edit Mode; TAA is disabled).

### 6.2 Camera Navigation

The editor camera is a free-fly camera controlled by mouse and keyboard while the viewport window is **hovered and right-mouse-button is held**:

| Input                         | Action                              |
|-------------------------------|-------------------------------------|
| Right-click + WASD            | Fly forward/left/back/right         |
| Right-click + Q / E           | Fly down / up                       |
| Right-click + mouse move      | Look around (yaw/pitch)             |
| Scroll wheel                  | Zoom (adjust fly speed multiplier)  |
| Middle-click + drag           | Pan (strafe + vertical translate)   |
| F (entity selected)           | Frame selection (focus camera on selected entity's bounding box) |
| Alt + left-click + drag       | Orbit around selection              |

Camera speed is configurable in Editor Preferences. Hold `Shift` to multiply fly speed by 3x for traversing large levels.

### 6.3 Entity Selection via Viewport

Left-clicking in the Viewport performs a **screen-space ray cast** against the scene. The ray is constructed from the click position and the editor camera's view-projection matrix. The first entity whose bounding box or triangle mesh is intersected is selected. A GPU-side object-ID render target is used for pixel-perfect selection (each entity is assigned a unique 32-bit ID drawn to the selection buffer during a pre-pass; the clicked pixel's ID is read back via `ReadbackBuffer`).

### 6.4 Transform Gizmos

Transform gizmos are implemented via **ImGuizmo** (bundled). The active gizmo type is toggled by keyboard shortcut:

| Key | Gizmo Mode   |
|-----|--------------|
| W   | Translate    |
| E   | Rotate       |
| R   | Scale        |
| Q   | No gizmo (selection-only mode) |

**Local vs. World Space** toggle: button in the toolbar (shortcut `X`). In Local mode the gizmo axes align to the entity's rotation. In World mode they align to world axes.

**Grid Snap** (hold `Ctrl`): snaps translate operations to the configured grid size (default 0.25 m). Snap values are configurable per-axis in Editor Preferences. Rotation snap defaults to 15-degree increments when `Ctrl` is held during a rotate operation.

Gizmo moves are pushed to the undo stack as `TransformGizmoMoveCommand` on mouse release (not on every frame drag), keeping the undo stack clean.

### 6.5 Viewport Overlays

Toggleable via the **View** menu or toolbar icon buttons:

| Overlay                   | Default | Description                                              |
|---------------------------|---------|----------------------------------------------------------|
| World Grid                | On      | Infinite ground-plane grid, fades with distance          |
| Collider Wireframes       | Off     | Draws physics collider shapes in green wireframe         |
| Entity Bounding Boxes     | Off     | Draws AABB for every entity                              |
| Spawn Point Markers       | On      | Colored arrows at each `SpawnPointEntity`                |
| Trigger Volume Outlines   | On      | Translucent colored boxes/spheres for trigger volumes    |
| Wireframe Mode            | Off     | Renders all geometry in wireframe                        |
| Navigation Mesh           | Off     | (Future) Displays AI navigation mesh overlay             |

---

## 7. Level Geometry Tools

The editor is designed around **mesh-based level construction**. There is no CSG (Constructive Solid Geometry) support; all geometry is either imported mesh assets or engine-provided primitives. This matches the engine's data model: all geometry that the renderer and physics engine consume is a triangle mesh or a convex/primitive shape.

### 7.1 Placing Static Mesh Entities

The primary workflow:

1. Open the **Asset Browser** and navigate to a `.easset` mesh.
2. Drag the asset from the Asset Browser into the 3D Viewport.
3. The editor creates a `StaticPropEntity` at the drag-drop hit point (ray cast against a virtual drop plane at Y=0 by default, or against existing geometry if any is present).
4. The new entity has: `TransformComponent` (identity), `MeshComponent` (the dragged asset), `TriMeshColliderComponent` (auto-generated from the mesh), and a `NameComponent` defaulting to the asset filename stem.
5. The entity is immediately selected; the translate gizmo is activated.

### 7.2 Box Primitive Tool (Blockout Geometry)

For rapid level blockout, the toolbar provides a **Box** primitive button (shortcut `B`):

1. Click the Box button. The cursor changes to a crosshair.
2. Click and drag in the Viewport to define the base rectangle (XZ plane).
3. Release, then move the mouse vertically and click to set the height.
4. The editor creates a `BlockoutBoxEntity` with: `TransformComponent`, `MeshComponent` (unit cube scaled to the drawn dimensions), `BoxColliderComponent` (matching half-extents), and a grey blockout material.

Blockout entities are functionally identical to static mesh entities and can be replaced with final art by swapping the `MeshComponent` asset.

### 7.3 Align and Distribution Tools

Available in the **Edit → Align** menu or via right-click with multiple entities selected:

| Tool                        | Action                                                       |
|-----------------------------|--------------------------------------------------------------|
| Snap to Grid                | Rounds position to nearest grid unit                         |
| Align to Face               | Moves entity so its bottom face sits on the first surface below it (ray cast downward) |
| Align Positions (X/Y/Z)     | Aligns all selected entities' positions on the chosen axis to the average, min, or max |
| Distribute Evenly (X/Y/Z)   | Spaces selected entities at equal intervals along the chosen axis |

All align operations are undoable.

### 7.4 Entity Grouping

Select multiple entities in the Hierarchy or Viewport, then:
- **Edit → Group Selection** (shortcut `Ctrl+G`): creates a new empty parent entity named "Group" and re-parents all selected entities under it. The group entity has only a `TransformComponent` and a `NameComponent`.
- **Edit → Ungroup** (shortcut `Ctrl+Shift+G`): dissolves the selected group, re-parenting all children to the group's original parent.

Groups are purely organizational. They do not affect rendering or physics. Moving a group entity moves all children via the standard parent-transform propagation in the ECS.

---

## 8. Player Start Positions

Spawn points are fundamental to every FPS game mode. The editor provides first-class tools for placing and managing them.

### 8.1 SpawnPointEntity

A `SpawnPointEntity` is an ECS entity with:

| Component           | Purpose                                        |
|---------------------|------------------------------------------------|
| `TransformComponent`| Position and facing direction of the spawn     |
| `TeamTagComponent`  | `Team::A`, `Team::B`, or `Team::Any`           |
| `NameComponent`     | Optional label shown in Level Settings list    |

The entity has no mesh or collider. It is editor-only in that it is rendered as a **colored directional arrow gizmo** in the Viewport (not a mesh asset). The arrow color encodes team:

- Team A: blue arrow
- Team B: red arrow
- Any: white arrow

The arrow points in the spawn entity's forward direction (+Z in local space), indicating which way the player will face on spawn.

### 8.2 Creating Spawn Points

- **Right-click in Viewport → Place → Spawn Point**: places a spawn at the cursor's hit point on the scene geometry, facing away from the surface normal.
- **Entity Palette drag**: the Entity Palette (future panel) will include a SpawnPoint entry.
- **Hierarchy right-click → Create Entity → Spawn Point**: creates a spawn at the world origin.

### 8.3 Team Assignment

In the Inspector, with a `SpawnPointEntity` selected, the `TeamTagComponent` widget shows a dropdown:

```
Team: [ Team A ▾ ]
       Team A
       Team B
       Any
```

Changing the team updates the arrow color in the Viewport immediately.

### 8.4 Level Validation

The editor validates spawn points on **Scene Save** and on **entering PIE**. Validation rules:

| Rule                                   | Severity | Message                                                 |
|----------------------------------------|----------|---------------------------------------------------------|
| At least 1 Team A spawn point          | Warning  | "Scene has no Team A spawn points."                     |
| At least 1 Team B spawn point          | Warning  | "Scene has no Team B spawn points."                     |
| No spawn point inside solid geometry   | Warning  | "Spawn point '{name}' may be inside geometry."          |

Warnings appear in the Console/Log panel. They do not block saving or PIE.

### 8.5 Level Settings Panel — Spawn Point List

The **Level Settings** panel (accessible via **Scene → Level Settings**) contains a dedicated "Spawn Points" section listing every `SpawnPointEntity` in the scene:

```
Spawn Points
 [ Team A ] SpawnPoint_0    (12.0, 0.0, -4.5)    [ Select ] [ Delete ]
 [ Team A ] SpawnPoint_1    (-8.0, 0.0,  3.0)    [ Select ] [ Delete ]
 [ Team B ] SpawnPoint_2    (0.0,  0.0, 30.0)    [ Select ] [ Delete ]
```

Clicking Select frames the entity in the Viewport and selects it.

---

## 9. Asset Browser

The Asset Browser provides a project-relative view of all engine-recognized file types and is the integration point between the file system and the ECS scene.

### 9.1 Supported File Types

| Extension           | Type                     | Icon Color |
|---------------------|--------------------------|------------|
| `.easset`           | Compiled mesh asset      | Teal       |
| `.scene`            | Scene file               | Green      |
| `.physics_material` | Physics material         | Orange     |
| `.toml`             | Config / project file    | Grey       |
| `.glb` / `.gltf`    | Raw import source        | Purple     |

Files of unsupported types are shown in the browser but are greyed out and non-interactive.

### 9.2 Importing Assets

**Import via drag-and-drop:**

1. Drag a `.glb` or `.gltf` file from Windows Explorer into the Asset Browser panel.
2. The editor calls `engine::tools::AssetImporter::import(srcPath, destDir)`.
3. A progress overlay appears while the importer runs (it is threaded; the editor remains responsive).
4. On completion, the resulting `.easset` file appears in the browser at the destination directory.
5. Import errors are reported in the Console/Log panel.

**Import via menu:**
**Asset → Import Asset...** opens a file picker filtered to `.glb` and `.gltf`.

### 9.3 Mesh Preview Window

Double-clicking a `.easset` file opens a floating **Mesh Preview** window:

- A small DX12 render target (256×256 by default, resizable) shows the mesh rotating slowly.
- The preview uses a simple three-point lighting rig; no scene lights affect it.
- The preview shows: mesh name, file size, vertex count, triangle count, bounding box dimensions.
- A **Replace In Scene** button replaces the selected scene entity's `MeshComponent` with this asset.

### 9.4 Search and Filter

- A search bar at the top of the Asset Browser filters by filename substring.
- A **Type** dropdown filters to a single asset type (All / Mesh / Scene / Physics Material / Config).
- Results are sorted alphabetically by filename within each directory.

### 9.5 Asset Metadata

The Asset Browser shows a metadata footer when an asset is selected:

```
  Selected: player_rifle.easset
  Path:     assets/meshes/weapons/player_rifle.easset
  Size:     1.24 MB
  Vertices: 8,320
  Triangles: 14,512
  Bounds:   (0.42 × 0.18 × 0.95) m
```

Metadata is stored in a sidecar `.easset.meta` JSON file generated by the importer and cached; it is not re-read from the binary asset on every browser refresh.

### 9.6 Right-Click Context Menu

| Menu Item     | Action                                                                            |
|---------------|-----------------------------------------------------------------------------------|
| Re-import     | Re-runs `AssetImporter` on the original source file (source path stored in meta) |
| Rename        | Opens an inline rename field; updates all `MeshComponent` references in the current scene (with warning dialog) |
| Delete        | Deletes the file; if referenced by any entity in the current scene, shows a warning listing referencing entities |
| Copy Path     | Copies the project-relative asset path to clipboard                               |
| Show in Explorer | Opens a Windows Explorer window focused on the asset's directory               |

---

## 10. Trigger Volumes

Trigger volumes define gameplay zones without visual geometry. They are essential to FPS game modes (bomb sites, control points, kill zones).

### 10.1 Creating a Trigger Volume

**Right-click in Viewport → Place → Trigger Volume** places a `TriggerVolumeEntity` at the cursor position.

The entity has:

| Component            | Purpose                                           |
|----------------------|---------------------------------------------------|
| `TransformComponent` | Position, rotation, scale of the volume           |
| `TriggerComponent`   | Shape type, dimensions, callback names            |
| `NameComponent`      | Label shown in hierarchy (e.g., "BombSite_A")     |

### 10.2 Visualization

In the Viewport, trigger volumes are drawn as **translucent colored geometry** (alpha ~0.15) with a solid wireframe border:

- Default color: cyan. The color is configurable per-volume via a color picker in the Inspector.
- Box triggers show as a box.
- Sphere triggers show as a sphere (tessellated icosphere for visibility).
- Capsule triggers show as a capsule.

The translucent rendering is done in the editor's debug draw pass and does not affect the game renderer.

### 10.3 Inspector Widget for TriggerComponent

```
[TriggerComponent]               [-]
  Shape:    [ Box ▾ ]
              Box
              Sphere
              Capsule
  Half Extents:  X [1.00]  Y [1.00]  Z [1.00]
  Color:         [   cyan   ]

  Callbacks:
    OnEnter:  [ BombSiteEntered___________ ]
    OnStay:   [                            ]
    OnExit:   [ BombSiteExited____________ ]
```

The callback name strings reference C++ functions registered via `TriggerCallbackRegistry::registerCallback("BombSiteEntered", &BombSiteEntered)`. The editor does not validate that the callback name resolves (it cannot; game.dll may not be loaded at edit time), but it highlights unresolved names in yellow at PIE start.

### 10.4 Common FPS Use Cases

| Scenario                  | Shape     | Callback Example                         |
|---------------------------|-----------|------------------------------------------|
| Bomb plant site           | Box       | `OnEnter: BombSiteActivated`             |
| Control point zone        | Box/Sphere| `OnStay: ControlPointCapturing`          |
| Out-of-bounds kill zone   | Box       | `OnEnter: KillPlayerOutOfBounds`         |
| Audio zone (reverb trigger)| Box      | `OnEnter: ApplyReverbZone`               |
| Loot pickup trigger       | Sphere    | `OnEnter: SpawnLootForPlayer`            |

---

## 11. Play-in-Editor (PIE)

### 11.1 Starting PIE

The **Play** button in the Toolbar (shortcut `F5`) begins PIE:

1. The editor validates the scene (spawn point check; see Section 8.4).
2. The current edit-mode world is serialized to an in-memory `SceneBuffer` (not written to disk).
3. A local server instance is created on a background thread using the engine's networking stack.
4. A local client instance is created and connects to the server (loopback, port 7777 by default).
5. The server loads the scene from the `SceneBuffer` and begins the tick loop.
6. The client's first-person camera takes over the Viewport. The editor UI panels remain visible and dockable but become read-only for scene hierarchy and Inspector (values shown are live game state).
7. The Toolbar shows a **Stop** button (red); all other toolbar buttons except Stop are disabled.

### 11.2 Debug Overlays During PIE

The following debug overlays are available while PIE is running (toggled via the **Debug** menu):

| Overlay                       | Description                                          |
|-------------------------------|------------------------------------------------------|
| Collision Shapes              | Green wireframe of all active physics colliders      |
| Entity Bounding Boxes         | Yellow AABBs around every entity                     |
| Network Entity Labels         | Shows each networked entity's `NetId` as world-space text |
| Frame Time Graph              | ImPlot graph of CPU/GPU frame times                  |
| Physics Step Counter          | Shows current physics step index and real-time delta |

### 11.3 Stopping PIE

The **Stop** button (shortcut `Shift+F5`) tears down PIE:

1. The client disconnects; the server shuts down.
2. The in-memory `SceneBuffer` is discarded.
3. The edit-mode ECS world is restored from the original on-disk `.scene` file (not from the pre-PIE in-memory snapshot, ensuring the on-disk file is always the source of truth).
4. The editor camera is restored to its last pre-PIE position.
5. All Inspector and Hierarchy panels return to edit mode.

### 11.4 PIE Limitations

- **Single local player only.** PIE spawns one client. Multi-player testing requires launching additional `GameClient.exe` instances externally.
- **No network lag simulation.** Loopback has zero simulated latency. Use the standalone network test tools for lag simulation.
- **Physics runs at real-time.** The physics timestep is not scaled; there is no slow-motion or pause during PIE (a future enhancement).
- **No hot-reload during PIE.** Game DLL changes are detected but applied only after Stop.
- **Console commands are available.** The Console/Log panel accepts engine console commands during PIE (e.g., `sv_gravity 0`, `debug_noclip 1`).

---

## 12. Undo/Redo System

### 12.1 Command Pattern

Every user-initiated operation that modifies editor or scene state is wrapped in a `Command` object implementing:

```cpp
struct ICommand {
    virtual void execute()  = 0;
    virtual void undo()     = 0;
    virtual std::string describe() const = 0; // e.g., "Move Entity player_start"
    virtual ~ICommand()     = default;
};
```

Commands are created by the editor subsystem that handles the user's input and immediately passed to `UndoStack::push(std::unique_ptr<ICommand>)`, which calls `execute()` and pushes the command onto the undo stack.

### 12.2 Undo Stack

The undo stack is a `std::deque<std::unique_ptr<ICommand>>` capped at **100 commands**. When the deque reaches 100, the oldest command is popped from the front and discarded.

A separate redo stack holds commands that have been undone. Pushing a new command **clears the redo stack** (standard behavior).

| Shortcut  | Action                          |
|-----------|---------------------------------|
| `Ctrl+Z`  | Undo last command               |
| `Ctrl+Y`  | Redo last undone command        |
| `Ctrl+Shift+Z` | Also Redo (alternative shortcut) |

The **Edit** menu shows the description of the next undoable and redoable command:
```
Edit
  Undo "Move Entity player_start"    Ctrl+Z
  Redo "Rotate Entity crate_01"      Ctrl+Y
```

### 12.3 Undoable Operations

| Operation                          | Command Type                       |
|------------------------------------|------------------------------------|
| Transform gizmo move/rotate/scale  | `TransformGizmoMoveCommand`        |
| Inspector field value change       | `InspectorChangeCommand<T>`        |
| Entity create (via menu/palette)   | `CreateEntityCommand`              |
| Entity delete                      | `DeleteEntityCommand`              |
| Hierarchy re-parent                | `ReparentEntityCommand`            |
| Entity rename                      | `RenameEntityCommand`              |
| Add component                      | `AddComponentCommand`              |
| Remove component                   | `RemoveComponentCommand`           |
| Scene globals change               | `SceneGlobalsChangeCommand`        |
| Bulk delete                        | `BulkDeleteCommand`                |
| Align / distribute                 | `AlignEntitiesCommand`             |
| Group / ungroup                    | `GroupEntitiesCommand`             |

### 12.4 Non-Undoable Operations

The following operations deliberately **do not** participate in the undo system:

| Operation        | Reason                                                          |
|------------------|-----------------------------------------------------------------|
| Scene Save       | On-disk state is the save point; undo past save is confusing    |
| Asset Import     | Filesystem operations are not reversible through the undo stack |
| PIE Start/Stop   | Simulation state transitions are not reversible                 |
| Asset Rename     | Filesystem operation (a separate "Rename Asset" undo could be added in a future pass) |

---

## 13. Editor Project Settings

### 13.1 project.toml

`project.toml` sits at the project root directory (the parent of the `assets/` folder). It is version-controlled alongside scene and asset files. It configures game-wide settings used both by the editor and the game runtime.

```toml
[project]
name            = "MyFPSGame"
version         = "0.1.0"
start_scene     = "assets/scenes/main_menu.scene"

[input]
default_bindings = "assets/config/input_default.toml"

[physics]
gravity          = [0.0, -9.81, 0.0]
fixed_timestep   = 0.016666

[networking]
default_port     = 7777
max_players      = 16

[rendering]
default_fov_deg  = 90.0
shadow_distance  = 80.0
```

The editor reads `project.toml` on startup and writes back only the `[project]` section when the user changes the project name via **Edit → Project Settings**. Other sections are left to hand-editing to avoid clobbering developer-tuned values.

### 13.2 Editor Preferences (editor_prefs.toml)

Editor preferences are **not** project data. They are stored per-user in `%APPDATA%\EngineEditor\editor_prefs.toml` and are not checked into source control.

```toml
[viewport]
grid_size        = 0.25       # meters; used for Ctrl-snap translate
angle_snap_deg   = 15.0
fly_speed        = 5.0        # m/s base camera fly speed
near_clip        = 0.1
far_clip         = 1000.0

[ui]
theme            = "dark"     # "dark" | "light" | "custom"
font_size_px     = 14
show_fps_counter = true

[recent_projects]
paths = [
    "C:/Projects/MyFPSGame/project.toml",
    "C:/Projects/TestMap/project.toml"
]

[recent_scenes]
paths = [
    "assets/scenes/level_01.scene",
    "assets/scenes/level_02.scene"
]
```

Preferences are saved automatically on editor exit. They can be reset to defaults via **Edit → Reset Editor Preferences**.

### 13.3 Project Settings Panel

**Edit → Project Settings** opens a floating panel that exposes a curated subset of `project.toml` fields:

| Field                   | Editable in Panel | Notes                                      |
|-------------------------|-------------------|--------------------------------------------|
| Game Name               | Yes               | Written back to `project.toml` on Apply    |
| Start Scene             | Yes               | File picker, must be a `.scene` path        |
| Default Input Bindings  | Yes               | File picker, must be a `.toml` path         |
| Gravity Vector          | Yes               | Also exposed in Scene Settings per-scene   |
| Fixed Physics Timestep  | Yes               | Expert setting; warn if below 0.008         |
| Default FOV             | Yes               | Forwarded to renderer on next PIE           |
| Max Players             | Yes               | Server cap                                 |

The panel has **Apply** and **Cancel** buttons. Apply writes changes to `project.toml` immediately. Cancel reverts to the on-disk values. Changes made via this panel are **not** pushed to the undo stack (project settings changes are considered configuration, not scene editing operations).

---

## Appendix: Summary of Keyboard Shortcuts

| Shortcut           | Action                                  |
|--------------------|-----------------------------------------|
| `Ctrl+N`           | New Scene                               |
| `Ctrl+O`           | Open Scene                              |
| `Ctrl+S`           | Save Scene                              |
| `Ctrl+Shift+S`     | Save Scene As                           |
| `Ctrl+Z`           | Undo                                    |
| `Ctrl+Y`           | Redo                                    |
| `W`                | Translate gizmo                         |
| `E`                | Rotate gizmo                            |
| `R`                | Scale gizmo                             |
| `Q`                | No gizmo (selection mode)               |
| `X`                | Toggle local / world gizmo space        |
| `F`                | Frame selected entity in Viewport       |
| `Ctrl` (hold)      | Grid snap (translate) / angle snap (rotate) |
| `B`                | Box primitive tool                      |
| `F5`               | Play-in-Editor                          |
| `Shift+F5`         | Stop Play-in-Editor                     |
| `Ctrl+G`           | Group selection                         |
| `Ctrl+Shift+G`     | Ungroup selection                       |
| `Delete`           | Delete selected entity/entities         |
| `Ctrl+D`           | Duplicate selected entity               |
| `Escape`           | Clear search filter / deselect          |

---

*Document maintained by the Tools Lead. Last updated Phase 6. Cross-reference: Physics+Scene lead owns `.scene` serialization format; Rendering lead owns DX12 render target and debug draw APIs.*
