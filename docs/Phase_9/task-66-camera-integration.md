# Task #66 — Camera System Integration

**Phase 9 — app module — Version 0.9.x**
**Audience:** Engine Lead, Rendering developer
**Depends on:** Phase 7 `MeshRenderSystem` (R2), `Camera` component, `Transform` component
**Unblocks:** #67 (viewport rendering), #74 (IGame bootstrap)

---

## 1. Goal

`MeshRenderSystem::tick()` currently has nowhere to get the view and projection matrices it needs to draw scene geometry. This task wires an active camera entity (a `Transform` + `Camera` component pair in the ECS) into the render system so that both the runtime game and the editor viewport can drive the camera from data, not from hardcoded matrices.

---

## 2. Current State

- `MeshRenderSystem` exists (`src/app/MeshRenderSystem.h/.cpp`) and holds a list of registered mesh handles, but `tick()` does not draw anything because it has no view/proj matrices.
- A `Camera` component type is referenced in `cameraViewMatrix(Transform&)` and `cameraProjMatrix(Camera&, aspect)` free functions (per CLAUDE.md).
- `Application` owns a `MeshRenderSystem` instance but never calls `tick()` with camera data.
- The editor's `EditorCamera` computes its own view/proj each frame for gizmo drawing but does not feed it into `MeshRenderSystem`.

---

## 3. Design

### 3.1 Camera entity query

On each render frame, `Application::run()` queries the world for the first entity that has both `Transform` and `Camera` components:

```cpp
core::Transform* camTransform = nullptr;
core::Camera*    camCamera    = nullptr;
core::ecs::View<core::Transform, core::Camera> view(world);
view.each([&](core::ecs::Entity, core::Transform& t, core::Camera& c) {
    if (!camTransform) { camTransform = &t; camCamera = &c; }
});
```

If no camera entity exists, `MeshRenderSystem::tick()` is skipped for that frame (no draw calls issued). A `LOG_WARN` is emitted once (not every frame) when the scene has mesh entities but no camera.

### 3.2 MeshRenderSystem::tick() signature

```cpp
// Before (stub):
void MeshRenderSystem::tick(rendering::FrameGraph& fg) { /* nothing */ }

// After:
void MeshRenderSystem::tick(rendering::FrameGraph& fg,
                            const core::math::Mat4& view,
                            const core::math::Mat4& proj);
```

### 3.3 Aspect ratio

Aspect ratio is derived from the swapchain dimensions exposed by `Engine` (already available via `Engine::swapchainWidth()` / `Engine::swapchainHeight()`). `Application` passes `(float)w / (float)h` to `cameraProjMatrix()`.

### 3.4 Camera component definition

Verify that `Camera` has the fields needed for `cameraProjMatrix`:

```cpp
struct Camera {
    float fovYDegrees = 60.0f;
    float nearPlane   = 0.1f;
    float farPlane    = 1000.0f;
};
```

If the struct doesn't exist yet, create `src/core/public/core/components/Camera.h`. It does **not** need a `kComponentId` — the camera is not replicated over the network and does not need serialisation in Phase 9.

---

## 4. Files to Modify

| File | Change |
|------|--------|
| `src/app/MeshRenderSystem.h` | Update `tick()` signature |
| `src/app/MeshRenderSystem.cpp` | Implement view/proj pass-through to the render pass |
| `src/app/Application.cpp` | Query camera entity; pass matrices to `meshRenderSystem_->tick()` |
| `src/core/public/core/components/Camera.h` | Create if missing |

---

## 5. Non-Goals

- Multiple simultaneous cameras (split-screen) — single active camera only in Phase 9.
- Camera component serialisation / replication — not needed until multiplayer camera authority is scoped.
- Culling against the camera frustum — `MeshRenderSystem` submits all registered handles; frustum culling is Phase 10.

---

## 6. Tests

**File:** `tests/app/CameraIntegrationTests.cpp` (label: unit)

- Headless device (`!device.isValid()` → `GTEST_SKIP`): create a world with a camera entity; verify `MeshRenderSystem::tick()` does not crash when called with identity matrices.
- No camera entity in world: verify `tick()` is skipped (no crash, no GL/DX call).
- Two camera entities: verify the first one found is used (order is archetype-stable, not insertion order — document this).

---

## 7. Known Issues

- **`MeshRenderSystem` lives in `src/app/`** — the editor also needs to drive mesh rendering for the viewport (#67). The editor links `engine::rendering` but not `engine::app`. See `risks-and-architecture.md §2` for the full module boundary analysis. Resolution is decided in #67 (the render system is either moved or the editor gets a local copy of the draw logic).
