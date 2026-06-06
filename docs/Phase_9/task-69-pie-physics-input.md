# Task #69 — PIE Physics + Input

**Phase 9 — editor — Version 0.9.x**
**Audience:** Editor developer, Gameplay Lead
**Depends on:** #68 (spawn system — needs a player entity to test against)
**Unblocks:** #74 (IGame bootstrap — PIE is the primary test path before a release build exists)

---

## 1. Goal

Pressing Play in the editor runs a physically-simulated game loop with WASD movement and mouse look. The player character moves, collides with level geometry, and the scene state is fully restored when Stop is pressed. Currently PIE ticks `Scene::tick()` but physics is not stepped and input is not routed.

---

## 2. Current State

- `PIEController::tick()` calls `scene_->tick(kFixedDt)` — `Scene::tick()` calls its `physicsStepFn_` if set, but `EditorApp` never sets it. Physics does not step.
- `EditorApp` does not own a `PhysicsWorld` instance. The runtime `Application` owns one (BW4, Phase 8), but the editor is a separate executable.
- `InputSystem` is part of `engine::core` and the editor links it, but nothing routes Win32 `WM_INPUT` / `WM_MOUSEMOVE` into `InputSystem` while PIE is active.
- `PIEController::stop()` restores ECS component snapshots, but the `PhysicsWorld` internal state (velocities, accumulated forces, broad-phase grid) is **not** restored — see §5.

---

## 3. PhysicsWorld in EditorApp

`EditorApp` needs its own `PhysicsWorld` instance for PIE. It follows the same pattern as `Application`:

**`src/editor/EditorApp.h`** — add:
```cpp
#include <physics/PhysicsWorld.h>
std::unique_ptr<physics::PhysicsWorld> physicsWorld_;
```

**`src/editor/EditorApp.cpp`** — in `init()`:
```cpp
physicsWorld_ = std::make_unique<physics::PhysicsWorld>();
```

When the editor activates a scene for PIE, wire the physics step function:
```cpp
void EditorApp::beginPIE() {
    if (!activeScene_) return;
    activeScene_->setPhysicsStepFn([this](float dt) {
        physicsWorld_->step(dt);
    });
    pie_.start(*activeScene_);
}
```

When PIE stops, clear the step function to prevent the scene from calling into the (now-stale or reset) physics world:
```cpp
void EditorApp::stopPIE() {
    pie_.stop();
    if (activeScene_) activeScene_->setPhysicsStepFn(nullptr);
    physicsWorld_->reset();  // see §5
}
```

---

## 4. Input Routing

### 4.1 Win32 message forwarding

The editor's `WinMain` / `Engine` message pump routes `WM_INPUT` to `Engine::processInput()`. During PIE, raw input must also reach `InputSystem`. Add a PIE-aware forwarding path in `EditorApp::onWindowMessage()` (or wherever the editor handles Win32 messages):

```cpp
if (pie_.isPlaying()) {
    inputSystem_.injectRawInput(wparam, lparam);
}
```

### 4.2 Mouse capture

`PIEController` already sets `captureMouse_ = true` on `start()`. Wire this to `SetCaptureMouse(hwnd)` / `ShowCursor(FALSE)` in EditorApp so the cursor is locked to the viewport during PIE and restored on stop.

### 4.3 InputSystem pump in PIEController

`PIEController::tick()` drives the fixed-step loop. Each step should flush the `InputSystem` for that tick:

```cpp
void PIEController::tick(float dt, core::input::InputSystem& inputSystem) {
    // ...
    while (accumulator_ >= kFixedDt) {
        inputSystem.tick();   // consume queued raw events into ActionState
        scene_->tick(kFixedDt);
        accumulator_ -= kFixedDt;
        ++simTick_;
    }
}
```

`PIEController::tick()` signature changes — update `EditorApp` call site.

---

## 5. PhysicsWorld State on PIE Stop

**This is a known correctness gap.** `PIEController::restoreSnapshot()` restores ECS component bytes (including `Transform`), so entity positions return to their pre-PIE values. However, `PhysicsWorld` internally tracks:

- Per-body velocities and accumulated forces
- Broad-phase grid cell occupancy
- Constraint solver warm-start data

After `restoreSnapshot()`, the ECS Transforms are correct but the physics body positions in `PhysicsWorld` still reflect the end-of-PIE state. The solution is a `PhysicsWorld::reset()` method:

```cpp
// In PhysicsWorld.h / .cpp
void PhysicsWorld::reset() {
    // Clear all body state. Bodies will be re-registered on next Scene::activate()
    // or on the first step() call after the scene re-activates.
    bodies_.clear();
    broadPhase_.clear();
    constraints_.clear();
}
```

After `pie_.stop()`, call `physicsWorld_->reset()` and then re-activate the scene's physics bodies by calling `activeScene_->activate()` on the physics subsystem only (or a new `Scene::reactivatePhysics()` method that re-registers all colliders without reloading assets).

**Phase 9 scope:** Implement `PhysicsWorld::reset()`. A full `Scene::reactivatePhysics()` can be a follow-up if the simpler reset is sufficient for editor use.

---

## 6. Files to Modify

| File | Change |
|------|--------|
| `src/editor/EditorApp.h` | Add `physicsWorld_`, `beginPIE()`, `stopPIE()` |
| `src/editor/EditorApp.cpp` | Create PhysicsWorld; wire physicsStepFn; handle PIE start/stop; mouse capture |
| `src/editor/PIEController.h` | Add `InputSystem&` parameter to `tick()` |
| `src/editor/PIEController.cpp` | Call `inputSystem.tick()` each fixed step |
| `src/physics/PhysicsWorld.h/.cpp` | Add `reset()` method |

---

## 7. Tests

**File:** `tests/editor/PIEPhysicsTests.cpp` (label: unit, headless)

- PIE start → tick 10 frames → stop: verify `Transform` of a falling entity returns to pre-PIE value (snapshot restore working).
- PIE start → `PhysicsWorld::step()` called: verify no crash with a simple box collider entity.
- `PhysicsWorld::reset()`: add a body, step once, reset; verify body count is zero.

---

## 8. Known Issues

- **Double-step risk:** `Scene::tick()` calls `physicsStepFn_` if set. `EditorApp` sets this function during PIE. If for any reason `GameLoop` is also instantiated in the editor (e.g., a future PIE networking mode), both would step physics. The clear separation is: `physicsStepFn_` is set only during PIE; GameLoop is not used in the editor at all. Document this invariant.
- **Input focus conflicts:** If a modal dialog opens during PIE (e.g., an error dialog), input routing to `InputSystem` should pause. Track `captureMouse_` state and suppress input injection when a modal is open.
