# Tools: Editor and Profiler

Status: Updated (Phase 10)
Owner: Tools Lead
Task: #15
References: architecture.md §8, ecs-design.md §10, rendering-frame-graph.md §3.3, scope-tools.md

---

## 1. Editor Overview

The editor is a full-featured scene editor compiled only in DevRel builds. It provides:

- **Docking layout** — Hierarchy (left), Viewport (center), Inspector (right), Asset Browser + Console (bottom), Scene Properties panel.
- **Hierarchy panel** — ECS entity list with name display and selection.
- **Viewport panel** — 3D viewport with ImGuizmo gizmo for translate, rotate, and scale (W/E/R keys; Ctrl for snap).
- **Inspector panel** — `ComponentEditorRegistry` drives per-component widgets for all 18 registered components.
- **Asset Browser panel** — Split view: 65% file list on the left, 35% inline mesh preview on the right (`MeshPreviewPanel::drawInline`). The separate floating "Asset Preview" window was removed.
- **Console panel** — Live log output.
- **Play-In-Editor (PIE)** — in-process server+client; see §8.
- **Undo/redo** — `UndoStack` with 100-command cap.
- **Scene save/load** and prefab system.

**The editor is DevRel-only.** All editor code is guarded by `#ifdef ENGINE_DEVREL`. In Debug and Release builds the guard compiles the editor to nothing. The editor window never opens in Release.

---

## 2. ImGui Integration

### 2.1 Dependencies

- `Dear ImGui` (docking branch) from vcpkg.
- `ImGuizmo` — from vcpkg (added in Phase 10 Wave 1, E1).

### 2.2 ImGui DX12 backend

The renderer provides one SRV slot in the main descriptor heap for the ImGui font texture (slot index stored in `GpuDevice`). The editor calls `ImGui_ImplDX12_Init(device, numFramesInFlight, backBufferFormat, mainHeap, cpuHandle, gpuHandle)` at startup.

The editor pass is the last pass in the frame graph (registered after all rendering passes):

```cpp
// Called from editor's bootstrap, only in DevRel
fg.addPass("EditorImGui", ...);   // see rendering-frame-graph.md §3.3
```

### 2.3 Per-frame editor update

Called from `app::Engine::tickFrame()` in DevRel, between `FrameGraph::execute()` and `GpuDevice::endFrame()`:

```cpp
ImGui_ImplDX12_NewFrame();
ImGui_ImplWin32_NewFrame();
ImGui::NewFrame();

editor_.update(world_);   // renders all editor windows

ImGui::Render();
// frame graph executes the ImGui pass here
```

The Win32 message handler forwards `WM_*` messages to `ImGui_ImplWin32_WndProcHandler` before the engine's own handler.

---

## 3. Editor Window Layout

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Engine Editor (DevRel)                                                   │
├────────────────┬───────────────────────────────────────┬─────────────────┤
│ [Hierarchy]    │ [Viewport]                            │ [Inspector]     │
│                │                                       │                 │
│  Player        │   <3D scene render>                   │  Transform      │
│  Camera        │                                       │    Position: …  │
│  Light         │   [ImGuizmo gizmo overlay]            │    Rotation: …  │
│  Mesh          │   W=translate E=rotate R=scale        │    Scale: …     │
│  ...           │                                       │                 │
│                │                                       │  MeshHandle     │
│                │                                       │    Path: …      │
│                │                                       │    Material: 0  │
├────────────────┴────────────────┬──────────────────────┴─────────────────┤
│ [Asset Browser]                 │ [Console]                              │
│  list (65%)  │ preview (35%)   │  log output                            │
└─────────────────────────────────┴────────────────────────────────────────┘
```

Panels use ImGui docking. The 3D viewport renders behind the docked UI. The viewport panel hosts the ImGuizmo overlay.

---

## 4. Entity List Panel

```cpp
void Editor::drawEntityListPanel(ecs::World& world) {
    ImGui::Begin("Entities");
    world.forEachEntity([&](ecs::Entity entity) {
        const char* name = "<unnamed>";
        if (auto* n = world.tryGet<core::Name>(entity)) name = n->str;

        bool selected = (entity == selectedEntity_);
        if (ImGui::Selectable(std::format("{} ({}:{})", name,
            entity.index, entity.generation).c_str(), selected))
            selectedEntity_ = entity;
    });
    ImGui::End();
}
```

`world.forEachEntity()` is an iteration over all live entities (a new World API needed from Team Leader: iterate alive entities without a component filter). If this API is not feasible from the ECS design, iterate a known base component like `core::Name` or `core::Transform` instead — discuss with Team Leader before implementing.

---

## 5. Component Inspector Panel

The inspector uses ECS reflection (ecs-design.md §10). For each component on the selected entity:

```cpp
void Editor::drawInspectorPanel(ecs::World& world) {
    if (!world.isAlive(selectedEntity_)) { selectedEntity_ = kInvalidEntity; return; }

    ImGui::Begin("Inspector");
    world.forEachComponentOnEntity(selectedEntity_, [&](ecs::ComponentTypeId typeId, void* data) {
        const auto& meta = world.getComponentMeta(typeId);
        if (ImGui::CollapsingHeader(meta.name, ImGuiTreeNodeFlags_DefaultOpen)) {
            if (meta.inspect) {
                ecs::EditorContext ctx{ world, selectedEntity_, gizmoOperation_ };
                meta.inspect(data, ctx);
            } else {
                ImGui::TextDisabled("(no inspector)");
            }
        }
    });
    ImGui::End();
}
```

`forEachComponentOnEntity` is a new World API (callback per registered component that the entity has). Add to the ECS task #19 requirement list.

### 5.1 Built-in component inspectors

The `inspect` function pointer for `core::Transform`:

```cpp
void inspectTransform(void* data, ecs::EditorContext& ctx) {
    auto& t = *static_cast<Transform*>(data);
    ImGui::DragFloat3("Position", &t.position.x, 0.01f);

    // Euler angles for display (convert from Quat, edit as Euler, convert back)
    float eulerDeg[3] = { ... };  // convert t.rotation to yaw/pitch/roll degrees
    if (ImGui::DragFloat3("Rotation", eulerDeg, 0.5f))
        t.rotation = fromEulerYxz(toRadians(eulerDeg[1]),
                                   toRadians(eulerDeg[0]),
                                   toRadians(eulerDeg[2]));

    ImGui::DragFloat3("Scale", &t.scale.x, 0.01f, 0.001f, 1000.0f);
}
```

### 5.2 Gizmo (ImGuizmo) — translate / rotate / scale

When the selected entity has a `Transform`, `ViewportPanel` draws an ImGuizmo gizmo overlaid on the 3D viewport. Keyboard shortcuts: **W** = translate, **E** = rotate, **R** = scale. Ctrl held during drag enables snap.

```cpp
ImGuizmo::SetOrthographic(false);
ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
ImGuizmo::SetRect(0, 0, viewportWidth, viewportHeight);

// ImGuizmo expects column-major matrices; convert from row-major storage
Mat4 viewCol  = transpose(viewMatrix);
Mat4 projCol  = transpose(projMatrix);
Mat4 worldCol = transpose(t.toMatrix());

if (ImGuizmo::Manipulate(
        &viewCol.m[0][0],
        &projCol.m[0][0],
        gizmoOperation_,   // TRANSLATE, ROTATE, or SCALE
        ImGuizmo::WORLD,
        &worldCol.m[0][0])) {
    // Decompose result back to row-major Transform
    Mat4 worldRow = transpose(worldCol);
    float matData[16]; memcpy(matData, &worldRow.m[0][0], 64);
    float translation[3], rotation[3], scale[3];
    ImGuizmo::DecomposeMatrixToComponents(matData, translation, rotation, scale);
    t.position = { translation[0], translation[1], translation[2] };
    t.rotation = fromEulerYxz(toRadians(rotation[1]),
                               toRadians(rotation[0]),
                               toRadians(rotation[2]));
    t.scale    = { scale[0], scale[1], scale[2] };
}
```

View and projection matrices are read from the main camera entity's `Camera` component.

---

## 6. Profiler Integration

### 6.1 CPU profiler scopes

```cpp
// tools/Profiler.h — includable from any module
namespace engine::tools {
    struct ProfilerScope {
        explicit ProfilerScope(const char* name);
        ~ProfilerScope();
    };
}

#if !defined(NDEBUG) || defined(ENGINE_DEVREL)
    #define PROFILE_SCOPE(name) ::engine::tools::ProfilerScope __prof##__LINE__{name}
#else
    #define PROFILE_SCOPE(name) ((void)0)
#endif
```

`PROFILE_SCOPE` is active in **Debug and DevRel** builds. It is elided only in Release (`NDEBUG` defined and `ENGINE_DEVREL` not defined).

Usage in any module:
```cpp
void renderSubmitSystem(ecs::World& world) {
    PROFILE_SCOPE("RenderSubmit");
    // ...
}
```

### 6.2 PIX markers

PIX integration uses `WinPixEventRuntime` (vcpkg). In DevRel builds only:

```cpp
#ifdef ENGINE_DEVREL
    #include <pix3.h>
    #define PROFILE_GPU_SCOPE(cmdList, name) PIXBeginEvent(cmdList, PIX_COLOR_DEFAULT, name)
    #define PROFILE_GPU_END(cmdList)         PIXEndEvent(cmdList)
#else
    #define PROFILE_GPU_SCOPE(cmdList, name) ((void)0)
    #define PROFILE_GPU_END(cmdList)         ((void)0)
#endif
```

`PROFILE_GPU_SCOPE` / `PROFILE_GPU_END` are wired in DevRel only (correct — PIX is a DevRel-only dependency).

Usage in the frame graph execute lambdas:
```cpp
[](ID3D12GraphicsCommandList* cmd, const PassResources& res) {
    PROFILE_GPU_SCOPE(cmd, "OpaquePass");
    // ... draw calls ...
    PROFILE_GPU_END(cmd);
}
```

The `#include <pix3.h>` is isolated in `tools/internal/PixWrapper.h` and included from rendering internals — never directly in `tools/Profiler.h`.

### 6.3 Tracy integration (optional build flag)

Tracy is NOT in `vcpkg.json` by default. Opt in by adding it to `vcpkg.json` and defining `ENGINE_TRACY=1` in the preset.

When `ENGINE_TRACY=1` is defined, `PROFILE_SCOPE` expands to `ZoneScoped` (Tracy's macro) instead of the `ProfilerScope` RAII type. The two profilers are mutually exclusive (Tracy and PIX don't coexist in v1).

Reminder from scope-tools.md: Tracy is a build-time flag, off by default. Adding it to CI is not required in v1.

---

## 7. File Layout

```
src/tools/
├── public/tools/
│   ├── Logger.h
│   ├── Config.h
│   ├── Profiler.h        — PROFILE_SCOPE, PROFILE_GPU_SCOPE macros
│   └── AssetImporter.h   — offline; not included by runtime

src/editor/               — separate static lib (engine_editor), DevRel-only
├── public/editor/
│   └── EditorApp.h       — EditorApp class (guarded by #ifdef ENGINE_DEVREL)
├── panels/
│   ├── HierarchyPanel.h/.cpp
│   ├── ViewportPanel.h/.cpp
│   ├── InspectorPanel.h/.cpp
│   ├── AssetBrowserPanel.h/.cpp
│   ├── MeshPreviewPanel.h/.cpp
│   ├── ConsolePanel.h/.cpp
│   └── ScenePropertiesPanel.h/.cpp
└── internal/
    ├── PIEController.h/.cpp
    ├── UndoStack.h/.cpp
    ├── ComponentEditorRegistry.h/.cpp
    └── EditorPrefs.h

src/tools/internal/
├── LoggerImpl.h
├── ConfigImpl.h
└── PixWrapper.h      — pix3.h include + forwarding
```

All editor source is inside `#ifdef ENGINE_DEVREL` guards so it compiles cleanly to nothing in Debug and Release.

---

## 8. Play-In-Editor (PIE)

PIE runs the game simulation in-process using a local server+client pair.

**Starting PIE:**
1. `PIEController::start()` snapshots the current scene state and places the player entity at the nearest `SpawnPointComponent`.
2. `EditorApp` checks `pieController_.isCapturingMouse()` (governed by `EditorPrefs::pieMouseCapture`, default `true`).
3. If mouse capture is enabled, `EditorApp` calls `ShowCursor(FALSE)`, `ClipCursor` (to the viewport rect), `SetCapture(hwnd)`, and sets `ImGuiConfigFlags_NoMouse` to prevent ImGui from consuming input.

**Stopping PIE:**
- Pressing **Escape** stops PIE.
- `PIEController::stop()` restores the scene snapshot taken at start, discarding any simulation changes.
- `EditorApp` releases cursor capture: `ClipCursor(nullptr)`, `ReleaseCapture()`, `ShowCursor(TRUE)`, clears `ImGuiConfigFlags_NoMouse`.

**Opt-out:** Set `EditorPrefs::pieMouseCapture = false` (or via `editor.toml`) to disable cursor capture — useful when debugging PIE without losing mouse focus.
