# Tools: Editor Stub and Profiler

Status: Approved (Phase 2)
Owner: Tools Lead
Task: #15
References: architecture.md §8, ecs-design.md §10, rendering-frame-graph.md §3.3, scope-tools.md

---

## 1. Editor Overview

The editor is a **stub** in v1. It provides:
- An ImGui-driven window showing the ECS entity list.
- A component inspector for the selected entity (driven by ECS reflection).
- A translate gizmo (ImGuizmo) for the `Transform` component.

It does NOT provide: undo/redo, save/load, play/stop, scene creation, asset browser.

**The editor is DevRel-only.** It is compiled only when `ENGINE_DEVREL` is defined. In Debug and Release builds, all editor code compiles to nothing (guarded by `#ifdef ENGINE_DEVREL`). The editor window never opens in Release.

---

## 2. ImGui Integration

### 2.1 Dependencies

- `Dear ImGui` (docking branch) from vcpkg.
- `ImGuizmo` — vendored under `engine/third_party/imguizmo/` (single-header, two files). Requires Team Leader approval to add; it is pre-approved for v1.

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
┌──────────────────────────────────────────────────────────────────────┐
│ Engine Editor (DevRel)                                               │
├──────────────────────────────────────────────────────────────────────┤
│ [Entity List]           │ [Inspector]                                │
│                         │                                            │
│  Entity 0 (Player)  ←──│  Transform                                 │
│  Entity 1 (Camera)     │    Position: [1.0] [0.0] [5.0]             │
│  Entity 2 (Light)      │    Rotation: [0.0] [0.0] [0.0]             │
│  Entity 3 (Mesh)       │    Scale:    [1.0] [1.0] [1.0]             │
│  ...                   │                                             │
│                         │  Renderable                                │
│                         │    Mesh: helmet.easset                    │
│                         │    Material: pbr_helmet                   │
│                         │    CastShadow: [x]                        │
└──────────────────────────────────────────────────────────────────────┘
                          ↑ gizmo overlay on the 3D viewport
```

Panels use ImGui docking. The editor is a floating ImGui window, not a fullscreen UI — the 3D viewport renders normally behind it.

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

### 5.2 Translate gizmo (ImGuizmo)

When the selected entity has a `Transform`, draw an ImGuizmo translate gizmo overlaid on the 3D viewport:

```cpp
ImGuizmo::SetOrthographic(false);
ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
ImGuizmo::SetRect(0, 0, viewportWidth, viewportHeight);

Mat4 worldMat = t.toMatrix();
if (ImGuizmo::Manipulate(
        &viewMatrix.m[0][0],
        &projMatrix.m[0][0],
        ImGuizmo::TRANSLATE,
        ImGuizmo::WORLD,
        &worldMat.m[0][0])) {
    Transform updated = Transform::fromMatrix(worldMat);
    t.position = updated.position;
    // do NOT modify rotation/scale from gizmo in translate-only mode
}
```

View and projection matrices are read from the main camera entity's `Camera` component.

Only the translate operation is supported in v1 (no rotate/scale gizmo). `gizmoOperation_` is always `ImGuizmo::TRANSLATE`.

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

#ifdef ENGINE_DEVREL
    #define PROFILE_SCOPE(name) ::engine::tools::ProfilerScope __prof##__LINE__{name}
#else
    #define PROFILE_SCOPE(name) ((void)0)
#endif
```

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

Usage in the frame graph execute lambdas:
```cpp
[](ID3D12GraphicsCommandList* cmd, const PassResources& res) {
    PROFILE_GPU_SCOPE(cmd, "OpaquePass");
    // ... draw calls ...
    PROFILE_GPU_END(cmd);
}
```

The `#include <pix3.h>` is inside `rendering/internal/` headers, never in `tools/Profiler.h`. `PROFILE_GPU_SCOPE` and `PROFILE_GPU_END` are macros in `tools/Profiler.h`; the `pix3.h` include happens inside the macro expansion via a platform header guard in the tools internal implementation. Alternatively: wrap PIX in `tools/internal/PixWrapper.h` and include that from rendering internals.

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
│   ├── AssetImporter.h   — offline; not included by runtime
│   └── editor/
│       └── Editor.h      — Editor class (DevRel-only; guarded by #ifdef ENGINE_DEVREL)
└── internal/
    ├── LoggerImpl.h
    ├── ConfigImpl.h
    ├── PixWrapper.h      — pix3.h include + forwarding
    └── editor/
        ├── EntityListPanel.h
        └── InspectorPanel.h
```

`Editor.h` is inside a `#ifdef ENGINE_DEVREL` guard at the header level so it compiles cleanly in Debug and Release without requiring any stub implementation.
