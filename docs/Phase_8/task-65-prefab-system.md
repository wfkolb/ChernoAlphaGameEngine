# Task #65 — Entity Prefab System

**Phase 8 — tools / editor — Version 0.8.x**
**Audience:** Tools Lead, Editor developer
**Depends on:** Task #51 (EntityFactory), Task #54 (SceneSerializer format), Task #64 (scene dirty flag + file pipeline)
**Resolves:** TD-07 (EntityFactory archetypes not serializable)

---

## Table of Contents

1. [Goal](#1-goal)
2. [What Is a Prefab?](#2-what-is-a-prefab)
3. [Prefab File Format](#3-prefab-file-format)
4. [PrefabSerializer](#4-prefabserializer)
5. [PrefabInstance — Override Tracking](#5-prefabinstance--override-tracking)
6. [Editor — Save as Prefab](#6-editor--save-as-prefab)
7. [Editor — Asset Browser Integration](#7-editor--asset-browser-integration)
8. [Editor — Drag-Drop Instantiation](#8-editor--drag-drop-instantiation)
9. [Editor — Inspector Overrides UI](#9-editor--inspector-overrides-ui)
10. [EntityFactory Integration](#10-entityfactory-integration)
11. [TD-07 Fix — Archetype Name in SceneSerializer](#11-td-07-fix--archetype-name-in-sceneserializer)
12. [New Files](#12-new-files)
13. [Undo/Redo Integration](#13-undoredo-integration)
14. [Tests](#14-tests)
15. [Non-Goals for Phase 8](#15-non-goals-for-phase-8)

---

## 1. Goal

A designer selects one or more entities in the `SceneHierarchyPanel`, right-clicks, and chooses **Save as Prefab…**. They name the prefab, pick a save location, and the engine writes a `.prefab` file. Later they drag that `.prefab` from the Asset Browser into any scene to spawn an instance with the same components and values. Modified fields on an instance are highlighted in the Inspector; a **Revert** button resets them to the prefab default.

---

## 2. What Is a Prefab?

A prefab is a **serialized entity subtree** with no `SceneGlobals`, no physics world, and no external scene references. It is a content asset in the same sense as a `.easset`.

Conceptually:
```
Prefab
├── Root entity: components + default values
├── Child entity 1: components + default values
└── Child entity 2: components + default values
```

A **prefab instance** in a scene is a root entity tagged with the source prefab path and a per-component override bitmask. At load time, the instance is rebuilt from the prefab defaults then overrides are applied on top.

Prefabs do **not** support nested prefabs (prefab within prefab) in Phase 8.

---

## 3. Prefab File Format

`.prefab` uses a subset of the `.scene` binary format: the same TOML header + binary sections, but only the Entity Table and Component SoA sections are present. `SceneGlobals`, Hierarchy, and Asset Ref Table sections are omitted or written as zero-length.

**TOML header (64 bytes, NUL-padded to 512 bytes):**
```toml
magic   = "ENGP"        # "Engine Prefab" — distinguish from ENGS scenes
version = 1
name    = "FpsCharacter"
author  = "wfkolb"
created = 2026-06-04T00:00:00Z
```

**Binary sections (same encoding as SceneSerializer):**
- Entity table: N rows of `(localEntityIndex, componentMask)`
- Component SoA: one block per component type present in any prefab entity

The root entity is always `localEntityIndex = 0`. Child entity parent references use local indices, not scene-global `EntityId` values (which are unstable across scenes).

**File extension:** `.prefab`
**Magic string:** `ENGP` (not `ENGS`)

---

## 4. PrefabSerializer

`src/tools/public/tools/PrefabSerializer.h` and `src/tools/PrefabSerializer.cpp`

```cpp
class PrefabSerializer {
public:
    struct PrefabData {
        std::string name;
        // Entity 0 = root; entity N has HierarchyComponent referencing parent by local index
        std::vector<EntitySnapshot> entities;
    };

    // Capture an entity subtree from a live World into PrefabData
    static PrefabData capture(EntityId root, const World& world);

    // Write PrefabData to disk
    static bool save(const PrefabData& data, const std::filesystem::path& path);

    // Read from disk
    static std::optional<PrefabData> load(const std::filesystem::path& path);

    // Instantiate a loaded PrefabData into a World, returning the root EntityId
    static EntityId instantiate(const PrefabData& data, const SpawnParams& params, World& world);

    // Validate file magic and CRC without full parse
    static bool validate(const std::filesystem::path& path);
};
```

### 4.1 `capture()`

Traverses the entity tree rooted at `root` using `HierarchyComponent`. For each entity:
- Records all component type IDs and raw bytes
- Converts `EntityId` parent references in `HierarchyComponent` to local indices (0 = root, 1 = first child, etc.)

### 4.2 `instantiate()`

For each entity in `PrefabData.entities` (in order, so parents before children):
1. Create a new `EntityId` in `world`
2. For each component: `world.addComponentRaw(entity, typeId, bytes)`
3. Fix up `HierarchyComponent.parent` from local index → new scene `EntityId`
4. Apply `SpawnParams.position` / `rotation` as an offset to the root entity's `Transform`

`instantiate()` does **not** apply `PrefabInstance` overrides — that is done by the calling editor code after instantiation.

---

## 5. PrefabInstance — Override Tracking

`src/core/public/core/ecs/PrefabInstance.h`

```cpp
struct PrefabInstance {
    static constexpr ComponentTypeId kComponentId = 11;  // verify against table

    char     sourcePrefabPath[256];   // relative to project root; NUL-terminated
    uint32_t overriddenComponents;    // bitmask: bit N = component ID N has overrides
};
```

`PrefabInstance` is added to the root entity of every prefab instance in a scene. Child entities do not carry `PrefabInstance` — they are identified as prefab children by walking the hierarchy.

**Override storage:** Overrides are stored as the current component values on the entity itself — there is no separate delta store. The `overriddenComponents` bitmask records *which* components differ from the prefab default; the actual overridden values are already in the standard component storage.

At save time, `SceneSerializer` detects `PrefabInstance` and stores only the overridden components in the entity row, plus a reference to the `.prefab` path. At load time, it calls `PrefabSerializer::instantiate()` first, then applies the stored overrides.

---

## 6. Editor — Save as Prefab

Triggered by right-clicking an entity in `SceneHierarchyPanel` → **Save as Prefab…**

```
1. Open SaveAs file dialog filtered to "*.prefab"
2. PrefabData data = PrefabSerializer::capture(selectedEntity, world)
3. data.name = <filename without extension>
4. PrefabSerializer::save(data, chosenPath)
5. Replace the original entity subtree with a prefab instance:
   a. Delete original entities (no undo record for this intermediate step)
   b. EntityId instance = PrefabSerializer::instantiate(data, originalParams, world)
   c. world.addComponent<PrefabInstance>(instance, { .sourcePrefabPath = relativePath })
6. Push SaveAsPrefabCommand onto UndoStack (undoes steps 5a–5c, restores original entities)
7. AssetBrowserPanel refreshes — the new .prefab file appears
```

**Why replace with an instance?** So the entity in the current scene immediately reflects the prefab relationship. The designer can see overrides immediately.

---

## 7. Editor — Asset Browser Integration

- `.prefab` files are listed in `AssetBrowserPanel` with a `[PREFAB]` icon
- Single-click: show preview panel with prefab name, entity count, component list, thumbnail (same capsule thumbnail as the source entity's mesh, if any)
- Right-click: **Open in Inspector** (read-only view of defaults), **Duplicate**, **Delete** (warns if any scene instance references it)
- Drag-drop into Viewport → see Section 8

---

## 8. Editor — Drag-Drop Instantiation

Dragging a `.prefab` from the Asset Browser and dropping it onto the `ViewportPanel`:

1. Raycast drop pixel against scene BVH to find world position (same as Task #60 model drop)
2. `PrefabData data = PrefabSerializer::load(prefabPath).value()`
3. `EntityId root = PrefabSerializer::instantiate(data, SpawnParams{ .position = dropPos }, world)`
4. `world.addComponent<PrefabInstance>(root, PrefabInstance{ .sourcePrefabPath = relativePath })`
5. Select the spawned root in `SceneHierarchyPanel`
6. Push `InstantiatePrefabCommand` onto `UndoStack`

---

## 9. Editor — Inspector Overrides UI

When the selected entity has a `PrefabInstance` component, `InspectorPanel` enters **Prefab Instance Mode**:

- A banner at the top of the Inspector: `[PREFAB] FpsCharacter.prefab  [Select Asset] [Unpack]`
  - **[Select Asset]** — highlights the `.prefab` in the Asset Browser
  - **[Unpack]** — removes `PrefabInstance`, makes the entity standalone (non-undoable; confirm dialog)
- Each component that differs from the prefab default is highlighted with a blue left border
- Modified components show a `[↺ Revert]` button that resets that component's values to the prefab default and clears its bit in `overriddenComponents`
- Unmodified components show a `[∅]` lock icon (still editable; editing marks the component as overridden)

### Override Detection

On each Inspector frame, for each component on the instance:
1. Load the prefab defaults via `PrefabSerializer::load()` (cached after first load per path)
2. Compare raw bytes: `memcmp(instanceBytes, prefabDefaultBytes, sizeof(Component))`
3. If different, the component is overridden

This per-frame comparison is O(component count × component size) — acceptable for the < 20 components a character has. Cache the prefab defaults in `InspectorPanel` by path to avoid re-parsing the file every frame.

---

## 10. EntityFactory Integration

Prefabs can be registered as named archetypes in `EntityFactory`. This allows spawning a prefab by name from game code (e.g., from a spawn point's archetype field):

```cpp
// At game init (or editor asset scan):
if (auto data = PrefabSerializer::load("assets/FpsCharacter.prefab")) {
    entityFactory.registerArchetype("FpsCharacter", [data](const SpawnParams& p, World& w) {
        return PrefabSerializer::instantiate(*data, p, w);
    });
}
```

`EditorApp` scans `assets/` for `.prefab` files at startup and registers them all. This means game code can call `entityFactory.spawn("FpsCharacter", params, world)` without knowing whether `FpsCharacter` is a code-defined archetype or a designer-authored prefab.

**Conflict resolution:** If both a code archetype and a `.prefab` share the same name, the code archetype wins (registered first). Log `LOG_WARN` when a prefab registration would shadow a code archetype.

---

## 11. TD-07 Fix — Archetype Name in SceneSerializer

In `SceneSerializer`, add an optional `archetypeName` string to the entity table row:

```
Entity Table Row (extended):
  entityHandle   : uint32
  componentMask  : uint256 (32 bytes)
  flags          : uint8    (bit 0: hasPrefabRef, bit 1: hasArchetypeName)
  [if hasPrefabRef]    prefabPathLen : uint16, prefabPath : char[N]
  [if hasArchetypeName] nameLen      : uint8, archetypeName : char[N]
```

This is a **format version bump** (`version = 2` in the TOML header). Old scenes (`version = 1`) are still loadable — the flags byte defaults to 0, and no archetype name or prefab ref is read.

`SceneSerializer::save()` writes `hasPrefabRef = true` when the entity has `PrefabInstance`. `SceneSerializer::load()` reconstructs prefab instances by:
1. Calling `PrefabSerializer::instantiate()` for the base
2. Applying stored component overrides from the row's component data

---

## 12. New Files

```
src/tools/public/tools/PrefabSerializer.h
src/tools/PrefabSerializer.cpp
src/core/public/core/ecs/PrefabInstance.h
src/editor/component_widgets/PrefabInstanceWidget.h / .cpp
```

### Modify Existing Files

- `src/tools/CMakeLists.txt` — add `PrefabSerializer.cpp`
- `src/editor/EditorApp.cpp` — scan assets/ for .prefab at startup; register with EntityFactory
- `src/editor/panels/InspectorPanel.cpp` — add prefab instance mode; register `PrefabInstanceWidget`
- `src/editor/panels/AssetBrowserPanel.cpp` — add `.prefab` file type; drag-drop handler
- `src/editor/panels/SceneHierarchyPanel.cpp` — add "Save as Prefab…" context menu item
- `src/app/Engine.cpp` — register `PrefabInstance` component
- `src/tools/SceneSerializer.cpp` — format version bump; hasPrefabRef flag; load/save prefab refs

---

## 13. Undo/Redo Integration

| User action | Command class | Undo behavior |
|-------------|---------------|---------------|
| Save as Prefab | `SaveAsPrefabCommand` | Restores original standalone entities; deletes .prefab file |
| Instantiate prefab (drag-drop) | `InstantiatePrefabCommand` | Destroys all spawned entities |
| Revert component to prefab default | `PrefabRevertComponentCommand` | Re-applies the previous override value |
| Unpack prefab | `UnpackPrefabCommand` | Not undoable (confirm dialog before executing) |

---

## 14. Tests

**File:** `tests/tools/PrefabSerializerTests.cpp` (label: unit)

- `capture()` of a 3-entity hierarchy → `PrefabData` has 3 entries; root index = 0
- `save()` writes `ENGP` magic; `validate()` returns true; wrong-magic file returns false
- `load()` round-trips all component values exactly (byte-for-byte `memcmp`)
- `instantiate()` in a headless World: entity has all expected components; child's `HierarchyComponent.parent` is the new root EntityId (not the old one)
- `instantiate()` with `SpawnParams.position = {1, 0, 0}`: root Transform.position == {1, 0, 0}
- EntityFactory: register prefab archetype; `spawn("FpsCharacter", ...)` creates expected entity
- SceneSerializer round-trip with a prefab instance: save scene containing one prefab instance; reload; verify `PrefabInstance.sourcePrefabPath` matches; verify overridden component value persists

**File:** `tests/editor/PrefabOverrideTests.cpp` (label: unit)

- No GPU: test override detection (`memcmp`) logic directly against mock component data
- Mark component as overridden; verify bit set in `overriddenComponents`; revert; verify bit cleared and value matches prefab default

---

## 15. Non-Goals for Phase 8

- **Nested prefabs** (a prefab containing another prefab instance) — adds significant complexity to override tracking; Phase 9 scope
- **Prefab auto-update** (changes to the `.prefab` file propagate to all scene instances) — Phase 9 scope; requires a dependency graph
- **Prefab variant system** (parameterized prefab with slots) — Phase 9 scope
- **Prefab merge conflicts in version control** — `.prefab` is binary; document that teams should not edit the same prefab concurrently; proper DVCS support is Phase 9
