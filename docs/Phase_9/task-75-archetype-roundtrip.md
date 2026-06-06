# Task #75 — TD-07: EntityFactory Archetype Round-Trip

**Phase 9 — tools / core — Version 0.9.x**
**Audience:** Tools Lead
**Severity:** 🟠 High — prefab override correctness depends on this
**Depends on:** Phase 8 PS1 (SceneSerializer v2 format with `hasArchetypeName`)
**Resolves:** TD-07 (EntityFactory archetypes not serializable)

---

## 1. Goal

When a scene is saved, each entity with a known archetype name stores that name in the entity table row (`hasArchetypeName = true`, added in Phase 8 PS1). When the scene is loaded, instead of deserialising raw component bytes directly, the loader first calls `EntityFactory::spawn(archetypeName)` to get the archetype defaults, then applies only the stored overrides on top. This means:

1. If an archetype gains a new component between save and load, existing scenes get the default value (forward-compatible).
2. Prefab override tracking works correctly — `memcmp` against the archetype baseline is valid.
3. The editor can show which fields differ from the archetype default, not just from an empty struct.

---

## 2. Current State

`SceneSerializer::load()` reads component bytes for each entity and calls `world.addComponentRaw()` directly. The `hasArchetypeName` flag is written by `save()` but **ignored by `load()`**. The loader has no reference to `EntityFactory`.

---

## 3. API Change: SceneSerializer::load()

The load function must accept an optional `EntityFactory*`:

```cpp
// Before:
bool SceneSerializer::load(const std::filesystem::path& path,
                            core::scene::Scene& scene);

// After:
bool SceneSerializer::load(const std::filesystem::path& path,
                            core::scene::Scene& scene,
                            core::ecs::EntityFactory* factory = nullptr);
```

When `factory == nullptr`, behaviour is unchanged (raw byte deserialisation). This keeps all existing callers (tests, demos) working without modification.

When `factory != nullptr` and the entity row has `hasArchetypeName = true`:
1. Call `factory->spawn(archetypeName, SpawnParams{}, world)` to create the entity with archetype defaults.
2. For each component stored in the row, call `world.addComponentRaw(e, typeId, bytes)` — this *overwrites* the default with the saved value.

Components present in the archetype but absent from the saved row retain their archetype default values.
Components present in the saved row but absent from the archetype are applied normally.

---

## 4. SpawnParams Reconstruction

`factory->spawn()` takes `SpawnParams` which includes `position` and `rotation`. These should be read from the entity's stored `Transform` component (if present) before the archetype spawn, or passed as zeroes and overwritten by the stored `Transform` in step 2. The latter is simpler — use zeroes and let the stored Transform override it.

---

## 5. Entity Identity Stability

`EntityFactory::spawn()` calls `world.createEntity()` internally, which may produce a different `EntityId` than was stored in the scene file. The existing `SceneSerializer` already handles this via the local-index remapping in the entity table — this task does not change that logic.

---

## 6. Editor Integration

`EditorApp` already has an `EntityFactory` instance. Pass it to `SceneSerializer::load()`:

```cpp
// EditorApp.cpp — openScene():
SceneSerializer::load(path, *scene, &entityFactory_);
```

`Application.cpp` — `wireScene()` / start scene load:
```cpp
SceneSerializer::load(path, *scene, &engine_->entityFactory());
```

`Engine` must expose `entityFactory()` getter (add if missing).

---

## 7. PrefabInstance Interaction

When an entity has both `hasArchetypeName` and `hasPrefabRef`, the load order is:
1. `EntityFactory::spawn(archetypeName)` — archetype defaults.
2. `PrefabSerializer::instantiate(prefabData)` — prefab defaults on top.
3. Stored component overrides — per-instance overrides on top of prefab.

This three-layer stack ensures that archetype → prefab → instance overrides are applied in the correct order.

---

## 8. Files to Modify

| File | Change |
|------|--------|
| `src/tools/public/tools/SceneSerializer.h` | Add `EntityFactory*` parameter to `load()` |
| `src/tools/SceneSerializer.cpp` | Implement archetype-first spawn when factory provided |
| `src/editor/EditorApp.cpp` | Pass `&entityFactory_` to `SceneSerializer::load()` |
| `src/app/Application.cpp` | Pass factory to `SceneSerializer::load()` |
| `src/app/Engine.h/.cpp` | Add `entityFactory()` getter |

---

## 9. Tests

**File:** `tests/tools/ArchetypeRoundTripTests.cpp` (label: unit)

- Register archetype "TestEntity" with components A (value=1) and B (value=2). Save scene with one TestEntity where B has been changed to value=5. Load with factory. Verify: A == 1 (archetype default), B == 5 (saved override).
- Archetype gains component C between save and load. Load scene: entity has C with archetype default; no crash.
- Load without factory pointer: raw bytes used; same behaviour as Phase 8.
- PrefabInstance + archetype: 3-layer stack applies correctly.
