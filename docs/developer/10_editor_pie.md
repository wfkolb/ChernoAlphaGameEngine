# 10 — Editor & PIE: `EngineEditor` and Play-in-Editor

`EngineEditor.exe` is a standalone Windows application for authoring scenes, placing entities, tweaking components, and running Play-in-Editor (PIE) sessions. It is only available in the `devrel` build configuration (`ENGINE_DEVREL` defined).

## Building the editor

```bash
cmake --preset devrel
cmake --build --preset build-devrel --target EngineEditor
build\devrel\src\editor\EngineEditor.exe
```

The editor links against all engine libraries (`engine::core`, `engine::rendering`, `engine::tools`, `engine::physics`) and uses Dear ImGui (docking branch) for all UI.

## Default layout

```
┌──────────────────────────────────────────────────────────────┐
│  Toolbar: Play ▶  Stop ■  Save  Build  Settings              │
├──────────────┬───────────────────────────┬───────────────────┤
│              │                           │                   │
│  Scene       │      3D Viewport          │   Inspector       │
│  Hierarchy   │  (DX12 render-to-texture) │                   │
│              │                           │                   │
├──────────────┴───────────────────────────┴───────────────────┤
│  Asset Browser                │  Console / Log               │
└───────────────────────────────┴──────────────────────────────┘
```

Panels are dockable ImGui windows. Drag any panel tab to rearrange. The layout is saved to `editor_layout.ini`. Reset via **View → Reset Layout**.

## Scene Hierarchy panel

The left panel shows every entity in the active scene as a tree. Entities with children are collapsible.

- **Click** — select entity (shown in Inspector)
- **Ctrl+Click** — multi-select
- **Right-click** → context menu:
  - Add Entity (opens archetype picker)
  - Duplicate
  - Delete (Delete key also works)
  - Rename
- **Search box** (top of panel) — filters the tree by entity `Name`
- **Drag** an entity onto another — reparent (sets Transform parent)

## Inspector panel

The right panel shows all components on the selected entity.

- Each component has a collapsible header with a **×** button to remove it.
- **Add Component** button at the bottom — opens a searchable list of all registered component types.
- Numeric fields: click+drag to scrub; double-click to type a value.
- `Vec3` fields: individual X/Y/Z floats; click the chain icon to lock aspect ratio for scale.
- `Quat` rotation is displayed as Euler angles (degrees) and converted on edit.
- Color fields: click to open a color picker.
- Asset handle fields: drag from Asset Browser to fill; click the **×** to clear.

### Custom component widgets

Register a custom editor widget for your component types using `ComponentEditorRegistry`:

```cpp
// In your editor plugin or EditorApp initialization (ENGINE_DEVREL only)
#ifdef ENGINE_DEVREL
engine::editor::ComponentEditorRegistry::registerWidget<WeaponComponent>(
    [](engine::editor::InspectorContext& ctx, WeaponComponent& weapon) {
        ImGui::SliderFloat("Damage",  &weapon.damage,    0.f, 200.f);
        ImGui::SliderInt("Mag Size",  &weapon.magSize,   1, 60);
        ImGui::SliderFloat("Fire Rate Hz", &weapon.fireRateHz, 1.f, 30.f);
        if (ImGui::Button("Reset to Defaults"))
            weapon = WeaponComponent{};
    });
#endif
```

## Viewport panel

The center panel renders the scene using the full DX12 pipeline into an off-screen render target displayed via `ImGui::Image`.

### Editor camera controls (right-click to enter fly mode)

| Input | Action |
|-------|--------|
| Right-click + W/A/S/D | Fly forward/left/back/right |
| Right-click + Q/E | Fly down/up |
| Right-click + mouse | Look around |
| Mouse wheel | Zoom (adjusts fly speed) |
| F | Frame selection (move camera to selected entity) |
| Alt + Left-click + drag | Orbit around selection |

### Transform gizmos (ImGuizmo)

| Key | Mode |
|-----|------|
| W | Translate |
| E | Rotate |
| R | Scale |
| Q | Deselect / none |
| Ctrl | Snap (uses grid/angle snap settings) |

Snap settings live in **Edit → Preferences → Snap** (position in metres, rotation in degrees).

### Viewport overlays

Toggle via the **Overlays** button in the viewport toolbar:

| Overlay | What it shows |
|---------|--------------|
| Grid | World-space grid at Y=0 |
| Colliders | Wireframe collision shapes |
| Bounding Boxes | AABB for each entity |
| Spawn Points | Icon at each `SpawnPointEntity` |
| Triggers | Tinted volume for trigger colliders |
| Nav Mesh | Navigation mesh overlay |

## Asset Browser panel

The bottom-left panel mirrors the project's `content/` directory.

- Supported asset types: `.easset` (mesh), `.scene`, `.glb`/`.gltf` (import)
- **Drag** a `.easset` into the viewport to spawn a `StaticPropEntity` at the drop point
- **Drag** a `.glb` to run the asset importer pipeline (converts to `.easset`) and then spawn
- **Right-click** a `.scene` file → **Open Scene** or **Add to Current Scene** (additive load)
- Double-click a `.easset` → opens Mesh Preview window

## Console panel

The bottom-right panel streams engine log output (same as the in-game `Logger`). Filter by severity (Trace / Info / Warning / Error) via the checkboxes at the top. Click a log line to copy it.

## Creating a new scene

1. **File → New Scene** (Ctrl+N) — creates an empty scene with default `SceneGlobals`.
2. The **Scene Settings** dialog opens automatically (also accessible via F4).
3. Fill in scene name, gravity, ambient light, spawn points, and match settings.
4. Place geometry: drag `.easset` meshes from Asset Browser into the viewport.
5. Place spawn points: **Add → SpawnPoint**, position and assign `TeamTag`.
6. **File → Save Scene** (Ctrl+S) — writes `content/maps/<scenename>.scene`.

## Editing `SceneGlobals`

Open **Scene → Scene Settings** (F4):

| Field | Notes |
|-------|-------|
| Scene Name | Identifier used by `SceneManager::getByName()` |
| Gravity | Default –9.81 m/s²; set 0 for zero-g modes |
| Ambient Color | Base fill light color |
| Fog Start / End | Linear fog distance in metres |
| Match Time Limit | 0 = no timer; value in seconds |
| Max Players | Used by session matchmaking |
| Game Mode | String passed to `IGameMode` factory |
| Navmesh Asset | Path to `.navmesh` file |

## Undo / Redo

All editor mutations are tracked by `UndoStack` (100-command cap).

| Shortcut | Action |
|----------|--------|
| Ctrl+Z | Undo |
| Ctrl+Y / Ctrl+Shift+Z | Redo |

Tracked operations: transform gizmo edits, component value edits (on mouse-up), entity add/delete/duplicate/reparent. Undo history is cleared when a new scene is opened or PIE starts.

## Play-in-Editor (PIE)

PIE lets you run the game inside the editor without building a standalone executable.

### Starting PIE

Click the **Play ▶** toolbar button or press **F5**.

What happens internally:
1. The editor serializes the current edit-mode world into an in-memory `.scene` buffer (no disk write).
2. A local server instance and a local client instance are spawned in-process on separate threads.
3. Physics simulation begins at 64 Hz.
4. The editor camera switches to the player's first-person camera.
5. The game's `IGame::onInit` callback fires, registering archetypes and systems.

### Stopping PIE

Click **Stop ■** or press **Shift+F5**.

1. The server and client threads are torn down.
2. The in-memory scene buffer is discarded.
3. The edit-mode world is restored from the **original on-disk scene file**.

PIE changes **never** persist to the saved scene. Design it → test it → stop → adjust → save as a distinct step.

### Debugging during PIE

- The `onDebugUI` hook is called every render frame during PIE. Use it to draw ImGui overlays (entity positions, stats, cheat buttons) without affecting ship builds.
- Engine log output continues to stream to the Console panel.
- GPU validation layers are active in the `devrel` build; DX12 errors appear in the Console with a red background.
- Physics collider wireframes can be toggled from the Overlays button while PIE is running.

### PIE caveats

- The editor does not write a `PlayerProfile` or `MatchRecord` during PIE — save system calls are no-ops.
- Networking is local loopback (127.0.0.1, configurable port). No real clients can connect during PIE.
- Only one PIE session can run at a time. Attempting to start a second triggers a warning.

---

That covers all ten layers of the arena FPS developer guide. For the full engine API reference, see the `docs/` root-level documents (`architecture.md`, `ecs-design.md`, `networking-architecture.md`, etc.) and the Phase 6 design docs in `docs/Phase_6/`.
