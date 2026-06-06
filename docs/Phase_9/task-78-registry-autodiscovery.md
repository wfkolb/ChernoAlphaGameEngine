# Task #78 — TD-13: ComponentEditorRegistry Auto-Discovery

**Phase 9 — editor — Version 0.9.x**
**Audience:** Editor developer
**Severity:** 🟡 Medium — developer experience; new components silently have no editor widget
**Depends on:** #74 (IGame bootstrap — component set should be stable before overhauling the registry)

---

## 1. Goal

Today, adding a new ECS component requires manually registering an ImGui widget in `EditorApp.cpp`. Forgetting this step means the component is invisible in the Inspector — no error, no warning. This task adds a startup assertion that fires immediately when a component has no widget, so the gap is caught at launch rather than by a confused designer.

Full compile-time reflection (a `ComponentTraits<T>` type-list) is a longer-term goal; the assertion is the Phase 9 deliverable.

---

## 2. Current State

`EditorApp::registerComponentWidgets()` contains a series of manual calls:
```cpp
componentRegistry_.registerWidget(core::Transform::kComponentId, ...);
componentRegistry_.registerWidget(core::Health::kComponentId, ...);
// ... 13 more ...
```

If a new component is registered in `Engine::init()` but no widget is registered here, the Inspector silently shows nothing for that component. There is no check.

---

## 3. Startup Assertion

After `Engine::init()` has registered all components and after `EditorApp::registerComponentWidgets()` has registered all widgets, add a validation pass:

```cpp
void EditorApp::validateComponentRegistry() {
    const uint32_t registeredCount = core::ecs::World::registeredComponentCount();
    for (uint32_t id = 0; id < registeredCount; ++id) {
        const auto& meta = core::ecs::World::getComponentMeta(id);
        if (meta.size == 0) continue;  // zero-size tag components have no data to display
        ENGINE_ASSERT(componentRegistry_.hasWidget(id),
            "Component ID %u (%s) has no registered editor widget. "
            "Add a widget in EditorApp::registerComponentWidgets().",
            id, meta.name);
    }
}
```

Call `validateComponentRegistry()` at the end of `EditorApp::init()`. In devrel builds, this fires at editor startup if any component is missing a widget. In debug builds, it also fires (debug is the development config). In release builds, the editor doesn't exist.

`ComponentRegistry::hasWidget(id)` is a trivial map lookup — add it if missing.

---

## 4. ComponentTraits<T> (Phase 9 short-term version)

As a lightweight complement to the assertion, define a `ComponentTraits` specialisation pattern so widget registration can be co-located with the component definition rather than centralised in `EditorApp`:

```cpp
// In each component header (optional but encouraged):
#ifdef ENGINE_DEVREL
template<>
struct editor::ComponentTraits<core::Transform> {
    static constexpr const char* displayName = "Transform";
    static bool drawWidget(void* data) {
        // ImGui fields
    }
};
#endif
```

`EditorApp::registerComponentWidgets()` can iterate a type-list and call `ComponentTraits<T>::drawWidget` for each. But this requires a compile-time type list of all components — a significant C++ metaprogramming effort.

**Phase 9 scope:** Do the assertion only. Document `ComponentTraits<T>` as the Phase 10 pattern. Leave `registerComponentWidgets()` as-is.

---

## 5. Files to Modify

| File | Change |
|------|--------|
| `src/editor/EditorApp.h` | Add `validateComponentRegistry()` declaration |
| `src/editor/EditorApp.cpp` | Implement `validateComponentRegistry()`; call at end of `init()` |
| `src/editor/panels/InspectorPanel.h` | Add `hasWidget(id)` to `ComponentEditorRegistry` |
| `src/editor/panels/InspectorPanel.cpp` | Implement `hasWidget()` |

---

## 6. Tests

**File:** `tests/editor/RegistryValidationTests.cpp` (label: unit)

- Register a component in a mock world with no widget: verify `validateComponentRegistry()` fires `ENGINE_ASSERT` (use `EXPECT_DEATH` in debug builds).
- Register a zero-size component with no widget: verify no assert (zero-size components are excluded).
- All Phase 9 components have widgets: verify `validateComponentRegistry()` passes for the full component set.
