# Task #62 — New Component Workflow

**Phase 8 — core / docs — Version 0.8.x**
**Audience:** All engineers, future contributors
**Depends on:** Tasks #49–#59 (component infrastructure complete)
**Resolves:** TD-13 (partially — documents the manual process and adds a guard assertion)

---

## Table of Contents

1. [Overview](#1-overview)
2. [Step 1 — Define the Struct](#2-step-1--define-the-struct)
3. [Step 2 — Assign a ComponentTypeId](#3-step-2--assign-a-componenttypeid)
4. [Step 3 — Register in Engine::init()](#4-step-3--register-in-engineinit)
5. [Step 4 — Write the Inspector Widget](#5-step-4--write-the-inspector-widget)
6. [Step 5 — Register the Widget](#6-step-5--register-the-widget)
7. [Step 6 — Write a Unit Test](#7-step-6--write-a-unit-test)
8. [Step 7 — Networking Replication (optional)](#8-step-7--networking-replication-optional)
9. [Step 8 — Add to an Archetype (optional)](#9-step-8--add-to-an-archetype-optional)
10. [The Missing-Widget Guard](#10-the-missing-widget-guard)
11. [Constraints Reference](#11-constraints-reference)
12. [Worked Example — InteractableComponent](#12-worked-example--interactablecomponent)

---

## 1. Overview

Adding a component to the engine requires touching several files in a fixed order. Skip any step and the result is either a compile error, a silent runtime failure, or an entity that cannot be inspected in the editor. This document walks through every step for a game-layer component; engine-layer components (added by Anthropic engineers) follow the same steps.

**Total files touched per component (minimum): 5**

```
src/<module>/public/<module>/components/MyComponent.h    — struct definition
src/app/Engine.cpp                                       — registration
src/editor/component_widgets/MyComponentWidget.h/.cpp   — inspector UI
src/editor/EditorApp.cpp                                 — widget registration
tests/<module>/MyComponentTests.cpp                      — unit test
```

---

## 2. Step 1 — Define the Struct

Create `src/<module>/public/<module>/components/MyComponent.h`.

Rules:
- The struct must be **trivially copyable** (`std::is_trivially_copyable_v<T> == true`). The ECS uses `memcpy` for archetype moves.
- No pointers, no `std::string`, no `std::vector`. Use fixed-size arrays or indices into external tables.
- Must have `static constexpr ComponentTypeId kComponentId = N;` where N is the next unused ID (see the ID table in CLAUDE.md).
- Place under the module's namespace (e.g., `engine::core`, `engine::physics`).

```cpp
#pragma once
#include <core/ecs/ComponentTypeId.h>
#include <cstdint>

namespace engine::core {

struct InteractableComponent {
    static constexpr ComponentTypeId kComponentId = 9;

    uint8_t  interactRadiusDm;   // radius in decimetres (0.1 m units), max 25.5 m
    uint8_t  interactGroupId;    // which interaction group this belongs to
    bool     isEnabled;
    bool     requiresLineOfSight;
    uint32_t promptStringId;     // index into localised string table
};

static_assert(std::is_trivially_copyable_v<InteractableComponent>,
              "ECS requires trivially copyable components");

} // namespace engine::core
```

**Sizing note:** Keep structs small. The ECS packs components in SoA chunks; a 1-byte field difference across 10,000 entities costs 10 KB of L2 cache lines per iteration. Prefer index + external table over inline strings.

---

## 3. Step 2 — Assign a ComponentTypeId

Open `CLAUDE.md`. Find the **Component ID Table**. Add your component to the next available ID row. **Order matters** — the serialization format stores IDs, and `Engine::init()` registration order must match.

Update the table before writing any code so team members reserving IDs can see the allocation.

Current next-available ID: **9** (as of Phase 8 start).

---

## 4. Step 3 — Register in Engine::init()

Open `src/app/Engine.cpp`. Find the block that registers `networking::NetworkIdentity` (ID 8, the last Phase 7 component). Add your registration immediately after:

```cpp
// ID 9
core::ecs::World::registerComponent<core::InteractableComponent>({
    "InteractableComponent",
    sizeof(core::InteractableComponent),
    alignof(core::InteractableComponent),
    [](void* ptr) { new(ptr) core::InteractableComponent{}; },
    nullptr,  // trivially destructible
    nullptr   // trivially movable
});
```

**Registration order is the serialization order.** Never insert a component between existing registrations — always append. If you need to deprecate a component, leave a tombstone registration (zero-size, no-op constructor) to preserve the ID slot.

---

## 5. Step 4 — Write the Inspector Widget

Create `src/editor/component_widgets/MyComponentWidget.h` and `.cpp`.

```cpp
// MyComponentWidget.h
#pragma once
#include <core/components/InteractableComponent.h>

namespace engine::editor {
void drawInteractableComponentWidget(engine::core::InteractableComponent& comp);
} // namespace engine::editor
```

```cpp
// MyComponentWidget.cpp
#include "MyComponentWidget.h"
#include <imgui.h>

namespace engine::editor {

void drawInteractableComponentWidget(engine::core::InteractableComponent& comp) {
    float radiusM = comp.interactRadiusDm * 0.1f;
    if (ImGui::DragFloat("Interact Radius (m)", &radiusM, 0.05f, 0.1f, 25.5f))
        comp.interactRadiusDm = static_cast<uint8_t>(std::clamp(radiusM / 0.1f, 0.f, 255.f));

    ImGui::DragScalar("Group ID", ImGuiDataType_U8, &comp.interactGroupId);
    ImGui::Checkbox("Enabled", &comp.isEnabled);
    ImGui::Checkbox("Requires Line of Sight", &comp.requiresLineOfSight);
    ImGui::InputScalar("Prompt String ID", ImGuiDataType_U32, &comp.promptStringId);
}

} // namespace engine::editor
```

Guidelines:
- Use `ImGui::DragFloat` / `DragScalar` rather than `InputFloat` — drags are faster for tuning
- Clamp inputs to the physically valid range of the field's underlying type
- For enum/flag fields, use `ImGui::Combo` with a string array
- Do not allocate heap memory in the widget — it is called every frame
- If the field is read-only at runtime (e.g., only set at spawn), add `ImGui::BeginDisabled(true)` around it

---

## 6. Step 5 — Register the Widget

Open `src/editor/EditorApp.cpp`. In the startup function (after `ComponentEditorRegistry` is initialized), add:

```cpp
#include "component_widgets/MyComponentWidget.h"
#include <core/components/InteractableComponent.h>

// ... in EditorApp::init():
ComponentEditorRegistry::registerWidget<core::InteractableComponent>(
    "Interactable",
    editor::drawInteractableComponentWidget
);
```

The string `"Interactable"` is the display name shown in the Inspector panel header.

---

## 7. Step 6 — Write a Unit Test

Create `tests/<module>/<MyComponent>Tests.cpp` with label `unit`.

Minimum test cases:
1. Default-constructed component has sane default values (no garbage from uninitialized fields)
2. `std::is_trivially_copyable_v<MyComponent>` is true (compile-time; put in a `static_assert` in the header too)
3. `kComponentId` value matches the ID table
4. The component can be added to a `World`, retrieved, and has the expected default values

```cpp
TEST(InteractableComponentTest, DefaultValues) {
    engine::core::InteractableComponent c{};
    EXPECT_EQ(c.interactRadiusDm, 0);
    EXPECT_FALSE(c.isEnabled);
}

TEST(InteractableComponentTest, TriviallyCopiable) {
    static_assert(std::is_trivially_copyable_v<engine::core::InteractableComponent>);
}

TEST(InteractableComponentTest, ComponentIdMatchesTable) {
    EXPECT_EQ(engine::core::InteractableComponent::kComponentId, 9u);
}

TEST(InteractableComponentTest, ECSRoundTrip) {
    engine::core::ecs::World world;
    auto entity = world.createEntity();
    world.addComponent<engine::core::InteractableComponent>(entity);
    auto* comp = world.getComponent<engine::core::InteractableComponent>(entity);
    ASSERT_NE(comp, nullptr);
    comp->isEnabled = true;
    EXPECT_TRUE(world.getComponent<engine::core::InteractableComponent>(entity)->isEnabled);
}
```

---

## 8. Step 7 — Networking Replication (optional)

If the component needs to be replicated to clients:

1. Open `src/networking/public/networking/ReplicatedComponentBit.h` and add a new bit:
   ```cpp
   inline constexpr uint32_t RCB_INTERACTABLE = (1u << 5);
   ```
2. Open `src/networking/ReplicationSystem.cpp` and add a serialization case in the per-entity delta snapshot builder.
3. Add wire-format documentation (byte layout) to the task doc or an inline comment.
4. Update the `ReplicationTests.cpp` fixture.

Not every component needs replication. Components that are server-only (damage processing, AI state) or client-only (local UI flags, audio cues) should **not** be replicated.

---

## 9. Step 8 — Add to an Archetype (optional)

If this component belongs on a standard FPS entity type, open `src/core/fps/FpsArchetypes.cpp` and add it to the appropriate `registerArchetype` lambda.

If it is a new archetype entirely:

```cpp
factory.registerArchetype("InteractableProp", [](const SpawnParams& p, World& w) {
    auto e = w.createEntity();
    w.addComponent<Transform>(e, Transform{ .position = p.position });
    w.addComponent<InteractableComponent>(e, InteractableComponent{ .isEnabled = true });
    return e;
});
```

Register the archetype name in `registerFpsArchetypes()` so it is available everywhere `EntityFactory` is used, including the editor's **Spawn Entity** menu.

---

## 10. The Missing-Widget Guard

To prevent the case where a component is registered in `Engine::init()` but no Inspector widget is registered (resolving TD-13 partially), add the following `ENGINE_ASSERT` in `InspectorPanel::drawComponents()`:

```cpp
for (auto typeId : world.getComponentTypes(selectedEntity)) {
    if (!ComponentEditorRegistry::hasWidget(typeId)) {
        ENGINE_ASSERT(false,
            "Component type %u has no registered editor widget. "
            "Add one in EditorApp::init() or the Inspector will silently skip it.",
            typeId);
    }
    ComponentEditorRegistry::drawWidget(typeId, world.getRawComponent(selectedEntity, typeId));
}
```

This assert fires in Debug and DevRel builds only. Release builds skip (no editor in Release). The message points directly at the fix.

---

## 11. Constraints Reference

| Rule | Reason |
|------|--------|
| Trivially copyable | ECS moves components via `memcpy` during archetype migrations |
| ID is append-only | Serialization format uses IDs; inserting would corrupt saved scenes |
| Registration order = ID order | `Engine::init()` registration index must equal `kComponentId` |
| No pointers in struct | Pointees are not migrated during archetype move; will dangle |
| No `std::string` / `std::vector` | Not trivially copyable; also breaks SoA layout |
| Widget registered before first frame | Inspector renders on frame 1; missing widget fires the guard assert |

---

## 12. Worked Example — InteractableComponent

This section walks through a complete, real addition that can serve as a copy-paste starting point.

### Scenario

An FPS map has interactable objects: doors, terminals, pickup pedestals. We need a component that marks an entity as interactable, sets a radius and a UI prompt string, and can be replicated so all clients know which objects are currently locked/unlocked.

### File Checklist

```
src/core/public/core/components/InteractableComponent.h    ← Step 1
CLAUDE.md (Component ID table)                             ← Step 2
src/app/Engine.cpp                                         ← Step 3
src/editor/component_widgets/InteractableWidget.h/.cpp     ← Step 4
src/editor/EditorApp.cpp                                   ← Step 5
tests/core/InteractableComponentTests.cpp                  ← Step 6
src/networking/ReplicatedComponentBit.h                    ← Step 7 (if replicated)
src/core/fps/FpsArchetypes.cpp                             ← Step 8 (if archetype)
```

### Estimated time

A senior engineer who has read this document: **60–90 minutes** including tests. A junior engineer on their first component: **half a day** — plan accordingly.
