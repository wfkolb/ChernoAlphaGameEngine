# Task #72 — Trigger Volume System

**Phase 9 — core / physics / editor — Version 0.9.x**
**Audience:** Gameplay Lead, Physics developer, Editor developer
**Depends on:** Phase 7 #52 (PhysicsWorld), Phase 7 #55 (IGameMode), #67 (viewport gizmos)
**Note:** `TriggerEntity` archetype exists in FpsArchetypes but has no dedicated component — this task adds one

---

## 1. Goal

Designers place trigger volumes in the scene (box or sphere regions). When an entity with a physics body enters or exits the volume, `IGameMode::onTriggerEnter/Exit()` is called. Game modes use this for objective zones, capture points, damage fields, and win-condition areas.

---

## 2. Current State

- `FpsArchetypes.cpp` registers a `TriggerEntity` archetype (just a `Transform` + name). There is no dedicated component.
- `PhysicsWorld` has `overlapSphere()` (Phase 7 spec) but no per-step overlap tracking.
- `IGameMode` has no trigger callbacks.
- Conflict: adding `TriggerComponent` creates two representations of "trigger" (the archetype convention and the new component). The archetype must be updated to add `TriggerComponent` automatically.

---

## 3. TriggerComponent

**File:** `src/core/public/core/components/TriggerComponent.h`

```cpp
namespace engine::core {

struct TriggerComponent {
    static constexpr ecs::ComponentTypeId kComponentId = 15;

    ColliderShape shape;         // Box or Sphere (TriangleMesh / ConvexHull not supported)
    uint8_t       teamFilter;    // 0 = any entity; 1–15 = only entities with matching TeamTag
    uint32_t      eventTag;      // game-mode-defined meaning (e.g. 0=objective, 1=damage)
    bool          oneShot;       // if true, fires once then becomes inactive
    bool          active;        // runtime: false after oneShot fires
};

} // namespace engine::core
```

Register in `Engine::init()` after `SpawnPointComponent` (ID 14):
```cpp
world_.registerComponent<core::TriggerComponent>();
```

---

## 4. FpsArchetypes Update

```cpp
factory.registerArchetype("TriggerEntity",
    [](Entity e, const SpawnParams& p, World& w) {
        w.addComponent<engine::core::Transform>(e, {p.position, p.rotation});
        engine::core::TriggerComponent tc{};
        tc.shape = ColliderShape::Box{ .halfExtents = {1, 1, 1} };  // 2m cube default
        w.addComponent<engine::core::TriggerComponent>(e, tc);
    });
```

---

## 5. IGameMode Interface Extension

**File:** `src/app/public/app/IGameMode.h`

```cpp
// Called server-side when a physics body enters a trigger volume.
// triggerEntity: the entity with TriggerComponent.
// enteringEntity: the entity whose collider overlapped.
virtual void onTriggerEnter(ecs::EntityId triggerEntity,
                             ecs::EntityId enteringEntity) {}

// Called when the entering entity exits the volume.
virtual void onTriggerExit(ecs::EntityId triggerEntity,
                            ecs::EntityId exitingEntity) {}
```

Default implementations are empty (no-op), so existing `IGameMode` implementations do not need to be updated.

---

## 6. PhysicsWorld Overlap Tracking

**File:** `src/physics/PhysicsWorld.h/.cpp`

At the end of each `step()`, run overlap queries for all registered `TriggerComponent` entities:

```cpp
// Per trigger, per step:
struct TriggerState {
    std::unordered_set<ecs::EntityId> inside; // entities currently overlapping
};
std::unordered_map<ecs::EntityId, TriggerState> triggerStates_;
```

Algorithm per step (runs after constraint solve):
1. For each entity with `TriggerComponent` (iterated via a `View<TriggerComponent, Transform>`):
   a. Run `overlapBox()` or `overlapSphere()` using the trigger's shape + transform.
   b. Compute `entered = newSet - prevSet`, `exited = prevSet - newSet`.
   c. Fire `EventBus` events for entered/exited.
   d. Update `triggerStates_[triggerEntity]`.
2. For `oneShot` triggers that fired at least one entry: set `active = false`; remove from overlap tracking.

`PhysicsWorld::step()` takes an `EventBus&` parameter (already the pattern for damage events) — or the trigger events are queued and flushed by `GameLoop` after the step.

**Performance:** Overlap queries are `O(triggers × dynamic_bodies)`. For Phase 9 content (< 20 triggers, < 64 dynamic bodies), this is trivially cheap. Scale concerns appear at 200+ triggers — document the threshold.

---

## 7. GameLoop Wiring

**File:** `src/app/GameLoop.cpp`

After `PhysicsWorld::step()`, flush the trigger event queue:

```cpp
while (auto evt = triggerEventQueue_.pop()) {
    if (evt.type == TriggerEventType::Enter)
        gameMode_->onTriggerEnter(evt.triggerEntity, evt.enteringEntity);
    else
        gameMode_->onTriggerExit(evt.triggerEntity, evt.exitingEntity);
}
```

---

## 8. Editor Gizmo (depends on #67)

**File:** `src/editor/panels/ViewportPanel.cpp`

When #67 lands, add trigger gizmo rendering via `ImDrawList`:
- **Box trigger:** draw 12 edges of the oriented bounding box (yellow wireframe).
- **Sphere trigger:** draw 3 great circles (XY, XZ, YZ planes) in yellow.
- Show gizmo even when entity is not selected (triggers are always visible in the editor).
- Drag handles on box face centres (same pattern as E5 ColliderWidget handles) to resize in-place.

A `TriggerResizeCommand` follows the same pattern as `ColliderResizeCommand` for undo.

---

## 9. Spawn Menu Integration

**File:** `src/editor/panels/SceneHierarchyPanel.cpp`

Add `TriggerVolume` to the Spawn context menu:

```cpp
if (ImGui::MenuItem("Trigger Volume")) {
    const Entity spawned = entityFactory_->spawn("TriggerEntity", params, world);
    // ...
}
```

---

## 10. Tests

**File:** `tests/physics/TriggerVolumeTests.cpp` (label: unit)

- Box trigger at origin (2m cube): sphere collider enters at (0.5, 0, 0); verify `onTriggerEnter` fires.
- Same sphere moves to (2, 0, 0) next step: verify `onTriggerExit` fires.
- `oneShot = true`: trigger fires on entry; verify `active = false` after; verify no second fire.
- `teamFilter = 1`: entity with `TeamTag.teamId = 2` overlaps; verify no event fires.
- `teamFilter = 0`: any entity fires events.
