# Task #71 — Entity Authoring Tools

**Phase 9 — editor — Version 0.9.x**
**Audience:** Editor developer
**Depends on:** Phase 8 editor shell (#58), UndoStack
**Unblocks:** Faster level assembly (QoL; no hard dependents)

---

## 1. Goal

Level designers can duplicate entities, select multiple entities at once, and snap placements to surfaces. These three features reduce the repetitive manual work of placing props and cover one of the most common editor friction points.

---

## 2. Entity Duplication (Ctrl+D)

### 2.1 Behaviour

Pressing Ctrl+D in `SceneHierarchyPanel` (or via Edit menu) duplicates the selected entity and all of its components. The duplicate is placed at the same position + a (0.5, 0, 0.5) metre offset so it is visually distinct from the original. The duplicate becomes the new selection.

Duplication is deep: if the entity has children (via `HierarchyComponent`), the entire subtree is duplicated with new `EntityId`s and `HierarchyComponent` parent references updated to the new IDs.

### 2.2 DuplicateEntityCommand

```cpp
class DuplicateEntityCommand : public ICommand {
public:
    DuplicateEntityCommand(ecs::EntityId source, ecs::World& world);
    void execute() override;   // creates duplicate entities
    void undo()    override;   // destroys them
private:
    ecs::EntityId             source_;
    ecs::World&               world_;
    std::vector<ecs::EntityId> created_;   // root + children
};
```

`execute()` iterates the subtree, calls `world_.createEntity()` for each node, then `world_.addComponentRaw()` for each component, then fixes up `HierarchyComponent` parent references. The root entity gets a (0.5, 0, 0.5) offset applied to its `Transform`.

### 2.3 Integration

```cpp
// SceneHierarchyPanel::draw() — keyboard shortcut handling:
if (ImGui::IsWindowFocused() &&
    ImGui::IsKeyDown(ImGuiKey_ModCtrl) &&
    ImGui::IsKeyPressed(ImGuiKey_D, false) &&
    selected != kInvalidEntity) {
    auto cmd = std::make_unique<DuplicateEntityCommand>(selected, world);
    cmd->execute();
    selected = cmd->createdRoot();
    undo.push(std::move(cmd));
    if (sceneDirty_) *sceneDirty_ = true;
}
```

---

## 3. Multi-Select

### 3.1 Selection model

Extend `SceneHierarchyPanel` from a single `selected` entity to a `std::vector<ecs::EntityId> selection`:

- **Click** — replace selection with clicked entity.
- **Ctrl+click** — toggle the clicked entity in the selection set.
- **Shift+click** — range-select all entities between the last clicked and the clicked.
- **Escape** — clear selection.

`InspectorPanel` shows the intersection of components common to all selected entities when multiple are selected. Per-component edit applies to all selected entities simultaneously.

### 3.2 Gizmo on multi-select

`ViewportPanel` computes the centroid of the selected entities' positions and places the gizmo there. A transform applied via the gizmo is decomposed into a delta and applied to each entity's `Transform` independently (i.e., the relative offset between entities is preserved).

### 3.3 Multi-TransformCommand

The existing `TransformCommand` takes a single entity. Add:

```cpp
class MultiTransformCommand : public ICommand {
public:
    MultiTransformCommand(std::vector<ecs::EntityId> entities,
                          std::vector<core::Transform> before,
                          std::vector<core::Transform> after,
                          ecs::World& world);
    void execute() override;  // apply after[] transforms
    void undo()    override;  // restore before[] transforms
};
```

`MultiTransformCommand` is pushed when the user releases the gizmo drag with >1 entity selected.

---

## 4. Snap-to-Surface (Shift+Place)

### 4.1 Behaviour

When the user holds Shift while placing an entity (via drag from Asset Browser into viewport, or via Shift+G "snap selected to ground"), the editor raycasts downward (-Y axis) from the entity's current position against the scene's static physics geometry. If the ray hits, the entity's `Transform.position.y` is set to the hit point's Y, and the entity's rotation is optionally aligned to the surface normal.

### 4.2 Implementation

```cpp
void ViewportPanel::snapSelectedToSurface(ecs::Entity selected,
                                           physics::PhysicsWorld& physics,
                                           ecs::World& world)
{
    auto* t = world.tryGet<core::Transform>(selected);
    if (!t) return;
    const auto hit = physics.raycast(
        t->position + Vec3{0, 5, 0},   // start 5m above
        Vec3{0, -1, 0},                // straight down
        100.0f);
    if (!hit.hasHit) return;

    const core::Transform before = *t;
    t->position.y = hit.point.y;
    // Optional: align rotation to surface normal (skip if normal is near-vertical)

    undo.push(std::make_unique<TransformCommand>(selected, before, *t, world));
    if (sceneDirty_) *sceneDirty_ = true;
}
```

**Keyboard shortcut:** Shift+G (matching Blender convention for "snap to ground").

---

## 5. Files to Modify

| File | Change |
|------|--------|
| `src/editor/panels/SceneHierarchyPanel.h/.cpp` | Add multi-select state; Ctrl+D handler |
| `src/editor/panels/InspectorPanel.h/.cpp` | Handle multi-select display (common components only) |
| `src/editor/panels/ViewportPanel.h/.cpp` | Centroid gizmo for multi-select; Shift+G snap |
| `src/editor/commands/DuplicateEntityCommand.h/.cpp` | New — duplicate command |
| `src/editor/commands/MultiTransformCommand.h/.cpp` | New — batch transform command |

---

## 6. Tests

**File:** `tests/editor/AuthoringToolTests.cpp` (label: unit)

- Duplicate a single entity: verify new entity has the same components; position is offset by (0.5, 0, 0.5); undo removes it.
- Duplicate a 3-entity hierarchy: verify all 3 new entities; child parent refs point to new IDs, not original.
- Multi-select transform: apply +1 on X to 3 entities; verify all 3 positions updated; undo restores all 3.
- Snap-to-surface: entity at Y=10 above a floor at Y=0; snap; verify entity Y ≈ 0.
