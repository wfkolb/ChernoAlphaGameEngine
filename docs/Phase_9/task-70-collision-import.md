# Task #70 — Collision from Mesh Import

**Phase 9 — tools / editor — Version 0.9.x**
**Audience:** Tools Lead, Physics developer
**Depends on:** Phase 8 #60 (EditorImporter), Phase 7 #52 (ColliderShape)
**Resolves:** Phase 8 "Generate Collision" checkbox was UI-only with no implementation

---

## 1. Goal

When a designer imports a `.glb` and checks "Generate Collision", the resulting `.easset` file contains collision geometry. On scene load, entities with a `MeshHandle` automatically receive a `ColliderComponent` if their `.easset` includes a collision section. No manual per-entity collider authoring is needed for standard level geometry.

---

## 2. Current State

- `AssetImportSettings::generateCollision` exists and is exposed in the import modal (E4, Phase 8).
- `EditorImporter::beginImport()` passes settings to `importGltf()` but `importGltf()` ignores `generateCollision`.
- No collision data is written to `.easset`.
- The `.easset` format has no collision section — this task adds one (format version bump required).

---

## 3. Collision Shape Strategy

Two shapes are appropriate for different use cases:

| Shape | When to use | Generation method |
|-------|-------------|-------------------|
| `ColliderShape::TriangleMesh` | Static level geometry (floors, walls) | All triangles from the mesh |
| `ColliderShape::ConvexHull` | Dynamic props, pickups | meshoptimizer convex hull |

The import settings dialog (Phase 8) already has a "Generate Collision" checkbox. Add a second option:

```cpp
enum class CollisionType { TriangleMesh, ConvexHull };
```

Default: `TriangleMesh` (appropriate for level geometry). Show as a combo box in the import modal when "Generate Collision" is checked.

---

## 4. .easset Format Extension

The current `.easset` is raw mesh data (vertices + indices). Add an optional collision section:

**Versioned header:**
```
[0..3]   magic     : "EASS"
[4]      version   : uint8  (bump to version 2)
[5..6]   flags     : uint16 (bit 0: hasCollision)
[8..11]  meshOffset: uint32  (byte offset to mesh section)
[12..15] meshSize  : uint32
[16..19] collOffset: uint32  (0 if !hasCollision)
[20..23] collSize  : uint32  (0 if !hasCollision)
```

**Collision section encoding:**

```
collisionType : uint8  (0=TriangleMesh, 1=ConvexHull)
vertexCount   : uint32
indexCount    : uint32
vertices[]    : float[3] × vertexCount  (position only, no normal/UV)
indices[]     : uint32 × indexCount
```

`loadEasset()` is updated to return:

```cpp
struct CpuMesh {
    std::vector<VertexStatic>  vertices;
    std::vector<uint32_t>      indices;
    // New:
    std::optional<CpuCollision> collision;
};

struct CpuCollision {
    CollisionType              type;
    std::vector<core::math::Vec3> vertices;
    std::vector<uint32_t>         indices;
};
```

`loadEasset()` is backward-compatible: version-1 files return `collision = std::nullopt`.

---

## 5. Collision Generation in importGltf()

**File:** `src/tools/AssetImporter.cpp`

When `settings.generateCollision == true`:

```cpp
if (settings.generateCollision) {
    CpuCollision coll;
    coll.type = settings.collisionType;

    if (settings.collisionType == CollisionType::ConvexHull) {
        // Use meshoptimizer to generate a convex hull.
        // meshopt_buildMeshlets is not the right call here; use a simple
        // iterative gift-wrapping or delegate to PhysicsWorld::buildConvexHull()
        // (acceptable coupling since tools links engine::physics for cooking).
        coll = buildConvexHull(cpuMesh.vertices);
    } else {
        // TriangleMesh: use vertices directly (strip UV/normals for smaller file).
        for (const auto& v : cpuMesh.vertices)
            coll.vertices.push_back(v.position);
        coll.indices = cpuMesh.indices;
    }

    result.collision = std::move(coll);
}
```

---

## 6. Auto-ColliderComponent on Scene Load

**File:** `src/core/scene/Scene.cpp`

In `Scene::activate()`, after the mesh load delegate is called for each `MeshHandle` entity, check if the loaded `.easset` has collision data:

```cpp
// Existing mesh load path:
scene.setMeshLoadFn([](Entity e, const std::string& path) {
    auto cpuMesh = tools::loadEasset(path);
    if (!cpuMesh) return;

    // Upload to GPU (existing)...

    // NEW: auto-attach ColliderComponent if collision data present
    if (cpuMesh->collision && !world.hasComponent<ColliderComponent>(e)) {
        ColliderComponent cc{};
        if (cpuMesh->collision->type == CollisionType::TriangleMesh) {
            cc.shape = ColliderShape::TriangleMesh{
                .vertices = cpuMesh->collision->vertices,
                .indices  = cpuMesh->collision->indices
            };
        } else {
            cc.shape = ColliderShape::ConvexHull{
                .vertices = cpuMesh->collision->vertices
            };
        }
        world.addComponent<ColliderComponent>(e, cc);
    }
});
```

The check `!world.hasComponent<ColliderComponent>(e)` ensures manually-authored colliders are not overwritten.

---

## 7. Import Modal Update

**File:** `src/editor/panels/AssetBrowserPanel.cpp`

Add the collision type combo when "Generate Collision" is checked:

```cpp
ImGui::Checkbox("Generate Collision", &importSettings_.generateCollision);
if (importSettings_.generateCollision) {
    ImGui::Indent();
    const char* types[] = { "Triangle Mesh (static)", "Convex Hull (dynamic)" };
    int idx = (int)importSettings_.collisionType;
    if (ImGui::Combo("Shape Type", &idx, types, 2))
        importSettings_.collisionType = (CollisionType)idx;
    ImGui::Unindent();
}
```

---

## 8. Files to Modify

| File | Change |
|------|--------|
| `src/tools/public/tools/AssetImporter.h` | Add `CollisionType` enum, `CpuCollision` struct, update `CpuMesh` |
| `src/tools/AssetImporter.cpp` | Implement collision generation |
| `src/tools/public/tools/EassetLoader.h` | Update `CpuMesh` struct |
| `src/tools/EassetLoader.cpp` | Parse collision section (version 2) |
| `src/core/scene/Scene.cpp` | Auto-attach ColliderComponent from .easset collision data |
| `src/editor/panels/AssetBrowserPanel.cpp` | Add collision type combo to import modal |

---

## 9. Tests

**File:** `tests/tools/CollisionImportTests.cpp` (label: unit)

- Import a unit-cube `.glb` with `generateCollision=true, type=TriangleMesh`: verify `.easset` version=2, `hasCollision` flag set, vertex count matches.
- Import with `generateCollision=false`: verify version=2, `hasCollision=false`, `loadEasset().collision == nullopt`.
- `loadEasset()` on a version-1 `.easset`: verify `collision == nullopt`, no crash.
- Scene activation with a mesh entity whose `.easset` has collision: verify `ColliderComponent` added automatically.
- Scene activation where entity already has `ColliderComponent`: verify it is not overwritten.

---

## 10. Open Questions

- **Q:** Should the `.easset` version bump break the existing tests that rely on the v1 format? Recommendation: no — `loadEasset()` checks the version byte and handles both; update the `SpinDemo` fixture `.easset` to v2 format as a separate step.
- **Q:** For very large meshes (>50k triangles), `TriangleMesh` collision cooking can be slow. Should it run async? Recommendation: the entire import runs on a background thread (Phase 8 `EditorImporter::future_`) — the collision generation is part of that background work. No additional async layer needed.
