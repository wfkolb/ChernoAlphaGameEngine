# Task #77 — TD-04: Navmesh Field Decision

**Phase 9 — tools / core — Version 0.9.x**
**Audience:** Team Lead (decision required), Tools Lead (implementation)
**Severity:** 🟠 High — dead serialized field is a maintenance trap
**Current state:** `LOG_WARN` added to `SceneSerializer` when `navmeshAsset` is non-empty (2026-06-05)

---

## 1. Goal

Resolve the `SceneGlobals::navmeshAsset` field that has been dead since Phase 7. Either implement it or remove it cleanly. A decision must be made before the release build ships (#74) to avoid serialising dead data in production scenes.

---

## 2. Current State

`SceneGlobals::navmeshAsset` (a path string) is defined, serialised, and deserialised. Nothing populates it (no baking tool) and nothing reads it at runtime. A `LOG_WARN` fires when loading a scene that has a non-empty value.

---

## 3. Options

### Option A — Remove the field

Remove `navmeshAsset` from `SceneGlobals`, the serialiser, and the scene properties panel. When bot AI is eventually scoped, add the field back as part of that task with a real baking pipeline.

**Pros:** Clean; no dead code; no misleading editor UI.
**Cons:** Scenes that somehow have the field set will silently lose it on next save. (Currently no such scenes exist since the field was never writable from the editor.)

**Format change:** `SceneSerializer` writes a zero-length or absent `navmeshAsset` section. Old scenes with the field are loaded (the existing skip-unknown logic handles it) but the value is not preserved.

### Option B — Implement basic navmesh baking

Add a "Bake Navmesh" button to `ScenePropertiesPanel` that invokes the `asset_cooker` with a navmesh pass. Store the result as `<sceneName>.nav` alongside the `.scene` file.

**Pros:** Feature-complete; no field removal churn.
**Cons:** Navmesh baking (Recast/Detour or custom) is a significant new subsystem. Full pathfinding runtime (querying, steering) is Phase 10 scope.

---

## 4. Recommendation

**Option A** unless bot AI is explicitly scoped for Phase 9. The field adds no value until the runtime query system exists. Remove now; restore in the AI phase.

---

## 5. Implementation (Option A)

### 5.1 SceneGlobals

Remove from `src/core/public/core/scene/SceneGlobals.h`:
```cpp
// Remove:
std::string navmeshAsset;
```

### 5.2 SceneSerializer

Remove the read and write of `navmeshAsset` from `SceneSerializer.cpp`. Old scenes that contain the field are handled by the existing unknown-section skip — no data loss for content that never had a real value.

Remove the `LOG_WARN` that was added (2026-06-05) since the field no longer exists.

### 5.3 ScenePropertiesPanel

Remove the navmesh asset path text field from the panel, if it was added. Check `ScenePropertiesPanel.cpp`.

---

## 6. Files to Modify (Option A)

| File | Change |
|------|--------|
| `src/core/public/core/scene/SceneGlobals.h` | Remove `navmeshAsset` field |
| `src/tools/SceneSerializer.cpp` | Remove navmeshAsset read/write; remove LOG_WARN |
| `src/editor/panels/ScenePropertiesPanel.cpp` | Remove navmesh UI if present |

---

## 7. Tests

No new tests required. Verify existing `SceneSerializerTests` still pass after the field removal. Specifically, any test that round-trips `SceneGlobals` should not reference `navmeshAsset`.
