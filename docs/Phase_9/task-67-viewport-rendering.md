# Task #67 — Editor Viewport 3D Rendering

**Phase 9 — editor / rendering — Version 0.9.x**
**Audience:** Rendering developer, Editor developer
**Depends on:** #66 (camera integration), Phase 8 MeshRenderSystem (R2), GpuDevice
**Unblocks:** All visual level authoring (#68 icons, #72 gizmos, #71 multi-select bounds)

---

## 1. Goal

The `ViewportPanel` currently shows a dark grey rectangle labelled "(scene render target unavailable)" because `sceneTextureSrv = 0` is hardcoded in `EditorApp`. After this task, the viewport shows the live 3D scene rendered with the editor camera (or the player camera during PIE). Level designers can see geometry, collider overlays, spawn point icons, and trigger volumes while building.

---

## 2. Current State

- `ViewportPanel::draw(..., uint64_t sceneTextureSrv, ...)` accepts an SRV handle and renders it via `ImGui::Image()`. When `sceneTextureSrv == 0` it shows a placeholder.
- `EditorApp` always passes `0` — comment reads "No offscreen scene RT is wired yet".
- `ThumbnailRenderer` exists but uses a sentinel-only cache (no pixel rendering).
- `MeshRenderSystem` is in `src/app/` — **the editor links `engine::rendering` but not `engine::app`**, which is the primary blocker (see §3.1).

---

## 3. Design

### 3.1 Module boundary resolution

`MeshRenderSystem` currently lives in `src/app/`. The editor (`EngineEditor.exe`) cannot link `engine::app` without violating the dependency graph. Two options:

**Option A — Move `MeshRenderSystem` to `engine::rendering`**
- Rename/move to `src/rendering/MeshRenderSystem.h/.cpp`.
- `engine::rendering` already exposes `MeshManager`; this is a natural fit.
- Callers (`Application.cpp`, `EditorApp.cpp`) both link `engine::rendering`.
- ✅ Cleanest long-term. Minimal change to call sites.

**Option B — Duplicate a lightweight draw call in `EditorApp`**
- Keep `MeshRenderSystem` in `engine::app`; write a simpler `EditorMeshPass` in `src/editor/`.
- ❌ Code duplication; two systems to maintain.

**Decision: Option A.** Move `MeshRenderSystem` to `engine::rendering` as part of this task.

### 3.2 Offscreen render target

`EditorApp` owns an offscreen render target sized to the last-known viewport dimensions:

```cpp
struct ViewportRT {
    rendering::GpuTexture  colorTex;     // DXGI_FORMAT_R8G8B8A8_UNORM
    rendering::GpuTexture  depthTex;     // DXGI_FORMAT_D32_FLOAT (reverse-Z)
    uint64_t               srvHandle;    // for ImGui::Image()
    uint32_t               width  = 0;
    uint32_t               height = 0;
};
ViewportRT viewportRT_;
```

Created in `EditorApp::init()` at a default size (1280×720). Recreated when the viewport panel reports a different content size (debounced — see §3.3).

### 3.3 Resize handling

`ViewportPanel` exposes its current content size via `contentWidth()` / `contentHeight()`. `EditorApp` compares these to the RT dimensions each frame:

```cpp
if (viewportPanel_.contentWidth()  != viewportRT_.width ||
    viewportPanel_.contentHeight() != viewportRT_.height) {
    if (/* debounce: size stable for 3+ consecutive frames */) {
        recreateViewportRT(viewportPanel_.contentWidth(),
                           viewportPanel_.contentHeight());
    }
}
```

Recreating a DX12 RT requires `GpuDevice::flush()` to drain the GPU before releasing the old resource. Frame budget impact is one stall per resize event; this is acceptable for an editor tool.

### 3.4 Render loop integration

Each editor frame, after `ImGui::NewFrame()` and before `ImGui::Render()`:

```cpp
if (activeScene_ && viewportRT_.srvHandle != 0) {
    const Mat4 view = pie_.isUsingPlayerCamera()
        ? playerCameraView()
        : camera_.viewMatrix();
    const Mat4 proj = camera_.projMatrix(
        (float)viewportRT_.width / (float)viewportRT_.height);

    meshRenderSystem_->tick(frameGraph_, view, proj,
                             viewportRT_.colorTex, viewportRT_.depthTex);
}
viewportPanel_.draw(world, selected_, camera_, picking_, undo_,
                    viewportRT_.srvHandle, &showViewport_);
```

`meshRenderSystem_` is a new member of `EditorApp` (mirrors `Application`). It is lazily initialised after the first `beginFrame()` (same MeshManager constraint as Application).

### 3.5 Entity icon overlays

After the 3D pass, before handing the SRV to ImGui, the `ViewportPanel` draws screen-space icons for special entity types using `ImDrawList`. These are overlaid on top of the rendered image:

| Component | Icon | Colour |
|-----------|------|--------|
| `SpawnPointComponent` | Circle + `S` label | Green |
| `TriggerComponent` | Circle + `T` label | Yellow |
| `Camera` | Circle + `C` label | Light blue |
| `core::ecs::Name` only (empty entity) | Dot | Grey |

Icon positions come from `worldToScreen()` (already implemented in `ViewportPanel.cpp`).

### 3.6 PIE camera switch

When `pie_.isUsingPlayerCamera()` is true, `EditorApp` finds the first entity with `Transform + InputReceiverComponent` and uses its Transform for the view matrix. This is already partially wired (P2, Phase 8) — this task completes it by feeding the resulting view into the RT render pass rather than just into the ImGui overlay camera.

### 3.7 ThumbnailRenderer — real pixel render

Replace the sentinel-only cache with a real render:

1. Load the `.easset` mesh via `loadEasset()`.
2. Upload to a temporary `MeshManager` instance.
3. Render one frame to a 128×128 offscreen RT using a fixed isometric camera (45° pitch, 45° yaw, auto-fit distance).
4. Readback the RT pixels via a staging buffer.
5. Write PNG to `<project>/.thumbnails/<sha256>.png` using `stb_image_write` (already a vcpkg dependency via other users).
6. Cache the `ImTextureID` (keep the DX12 SRV alive for the duration of the editor session).

---

## 4. Files to Create / Modify

| File | Change |
|------|--------|
| `src/rendering/MeshRenderSystem.h/.cpp` | Move from `src/app/`; update `tick()` signature to accept RT handles |
| `src/app/MeshRenderSystem.h/.cpp` | Delete (moved) |
| `src/app/Application.cpp` | Update include path |
| `src/editor/EditorApp.h` | Add `ViewportRT viewportRT_`, `MeshRenderSystem* meshRenderSystem_` |
| `src/editor/EditorApp.cpp` | Create RT, drive mesh render pass, pass SRV to ViewportPanel |
| `src/editor/ThumbnailRenderer.cpp` | Replace sentinel cache with real 128×128 render |
| `src/rendering/CMakeLists.txt` | Add MeshRenderSystem sources |
| `src/app/CMakeLists.txt` | Remove MeshRenderSystem sources |
| `src/editor/panels/ViewportPanel.cpp` | Add icon overlay rendering (§3.5) |

---

## 5. Tests

**File:** `tests/rendering/ViewportRTTests.cpp` (label: integration — requires GPU)

- Create offscreen RT; run mesh render pass with a unit cube; verify pixel at centre is not the clear colour.
- Resize RT from 1280×720 to 800×600: verify no GPU validation errors.

**File:** `tests/editor/ThumbnailRenderTests.cpp` (label: integration)

- Load known `.easset`; request thumbnail; wait for completion; verify PNG file written to `.thumbnails/`.
- Request thumbnail for missing file: verify graceful failure (no crash, no PNG written).

---

## 6. Open Questions

- **Q:** Should the viewport have its own depth buffer or share the swapchain depth? Recommendation: own depth buffer — the viewport RT is a separate surface from the swapchain back buffer; sharing would require synchronisation that adds complexity.
- **Q:** Should gizmo geometry (grid, collider wireframes, already drawn by ImDrawList) be drawn before or after the 3D pass? Recommendation: after — 3D pass clears the depth buffer; gizmos drawn on top via ImDrawList always overdraw correctly.
