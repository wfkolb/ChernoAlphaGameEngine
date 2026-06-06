# Task #60 — Model Importing in the Editor

**Phase 8 — tools / editor — Version 0.8.x**
**Audience:** Tools Lead, Editor developer
**Depends on:** Task #58 (EngineEditor), existing `asset_cooker` and `AssetImporter` pipeline

---

## Table of Contents

1. [Goal](#1-goal)
2. [Current State](#2-current-state)
3. [Import Pipeline Overview](#3-import-pipeline-overview)
4. [New Files](#4-new-files)
5. [Import Settings Dialog](#5-import-settings-dialog)
6. [Asset Browser Integration](#6-asset-browser-integration)
7. [Mesh Preview](#7-mesh-preview)
8. [Meta Sidecar File](#8-meta-sidecar-file)
9. [Drag-Drop into Viewport](#9-drag-drop-into-viewport)
10. [Error Handling](#10-error-handling)
11. [Tests](#11-tests)
12. [Open Questions](#12-open-questions)

---

## 1. Goal

A user drags a `.glb` or `.gltf` file from Windows Explorer onto the Asset Browser panel (or uses **Assets → Import Model…**). The editor invokes the `asset_cooker` pipeline, shows a progress dialog with import settings, writes the resulting `.easset` alongside a `.easset.meta` sidecar, and displays the mesh in the Asset Browser with a thumbnail. The user can then drag the asset from the browser into the Viewport to create a `StaticPropEntity`.

No changes to the offline `asset_cooker` binary itself. All new work is in the editor and in a thin in-process import wrapper.

---

## 2. Current State

- `asset_cooker/main.cpp` — standalone CLI that converts glTF 2.0 + PNG/HDR → `.easset`
- `src/tools/public/tools/AssetImporter.h` — `AssetImporter::importGltf(path, options) -> CpuMesh`; used internally by `asset_cooker`
- `src/editor/panels/AssetBrowserPanel.h/.cpp` — lists `.easset`, `.scene`, `.glb/.gltf`; has drag-drop stub but no import logic
- No `.easset.meta` sidecar exists today
- No mesh thumbnail system exists today

**Gap:** The Asset Browser can *display* `.glb` files but double-clicking or dragging them does nothing.

---

## 3. Import Pipeline Overview

```
User action (drag / menu)
        │
        ▼
ImportSettingsDialog
  (scale, up-axis, LOD count,
   generate collision, material slot names)
        │
        ▼
EditorImporter::importModel(path, ImportSettings)  ← new
        │
        ├─► AssetImporter::importGltf()            ← existing
        │         │
        │         └─► .easset  written to project assets/
        │
        ├─► MetaFileWriter::write(path + ".meta")  ← new
        │         └─► ImportSettings + SHA-256 of source file
        │
        └─► ThumbnailRenderer::renderMesh()        ← new
                  └─► 64×64 PNG thumbnail cached in assets/.thumbnails/
```

The `EditorImporter` runs on a background thread. The editor main loop polls for completion and shows a spinner in the Asset Browser row.

---

## 4. New Files

### `src/editor/EditorImporter.h / .cpp`

```
class EditorImporter {
public:
    struct ImportSettings {
        float      uniformScale     = 1.0f;
        UpAxis     upAxis           = UpAxis::Y;  // Y or Z
        bool       generateCollision = false;     // auto Box/Convex from mesh bounds
        bool       mergeMeshes      = false;      // collapse submeshes into one
        uint8_t    lodCount         = 1;          // stub: only 1 LOD generated in Phase 8
    };

    // Returns immediately; result available via pollResult()
    void beginImport(const std::filesystem::path& sourcePath, const ImportSettings& settings);

    struct ImportResult {
        bool                        succeeded;
        std::filesystem::path       eassetPath;
        std::string                 errorMessage;  // empty on success
    };

    bool             isImporting()  const;
    float            progress()     const;  // 0.0–1.0
    std::optional<ImportResult> pollResult();  // returns nullopt while in progress
};
```

One `EditorImporter` instance lives on `EditorApp`. Concurrent imports are serialized (queue-based, not parallel) to avoid overloading the asset_cooker pipeline.

### `src/editor/MetaFileWriter.h / .cpp`

Reads and writes `.easset.meta` TOML sidecars.

```
struct AssetMeta {
    uint64_t                   sourceModTime;    // Windows FILETIME of source .glb
    std::string                sourceSha256;     // hex string
    EditorImporter::ImportSettings settings;
};

class MetaFileWriter {
public:
    static void         write(const std::filesystem::path& eassetPath, const AssetMeta&);
    static AssetMeta    read(const std::filesystem::path& eassetPath);
    static bool         exists(const std::filesystem::path& eassetPath);
    static bool         isStale(const std::filesystem::path& eassetPath);  // checks source mtime
};
```

`isStale()` answers TD-15 (SHA-256 cache): `SceneSerializer` can call `MetaFileWriter::isStale()` before recomputing the content hash.

### `src/editor/ThumbnailRenderer.h / .cpp`

Renders a 64×64 RGBA PNG of a mesh for the Asset Browser. Runs on the editor's DX12 device using a minimal off-screen render target (a single flat-shade pass, no frame graph required).

```
class ThumbnailRenderer {
public:
    // Async — returns immediately; thumbnail file written when done
    void requestThumbnail(const std::filesystem::path& eassetPath);
    bool hasThumbnail(const std::filesystem::path& eassetPath) const;
    // Returns ImTextureID (nullptr if not ready)
    ImTextureID getImGuiTexture(const std::filesystem::path& eassetPath);
};
```

Thumbnails are cached in `<project_root>/assets/.thumbnails/<asset_name_hash>.png`. The hash is the SHA-256 from the `.meta` file, so thumbnails auto-invalidate on reimport.

---

## 5. Import Settings Dialog

Modal ImGui dialog opened before import begins. Fields:

| Field | Type | Default | Notes |
|-------|------|---------|-------|
| Uniform scale | float drag | 1.0 | Applied to all vertex positions |
| Up axis | combo (Y / Z) | Y | Remaps axis if source is Z-up (Blender default) |
| Generate collision | checkbox | false | Creates a `Collider` (Box from AABB) on import |
| Merge meshes | checkbox | false | Collapses submeshes; loses per-mesh material assignment |
| LOD count | int slider 1–4 | 1 | Phase 8 always generates 1 LOD; higher values are accepted but produce duplicates until Phase 9 adds LOD reduction |

**Cancel** aborts without writing anything. **Import** begins the background job.

---

## 6. Asset Browser Integration

### Modify `AssetBrowserPanel`

- Add `.glb` and `.gltf` file type registration (they are already listed but not importable)
- Double-click on a `.glb/.gltf` → open `ImportSettingsDialog`
- Drag `.glb/.gltf` from Windows Explorer → open `ImportSettingsDialog`
- Show spinner overlay on the `.glb` row while `EditorImporter::isImporting()` is true
- After import completes, replace the `.glb` row with the new `.easset` row and show its thumbnail
- Right-click context menu on `.easset` → **Reimport** (reopens dialog with settings read from `.meta`)

### Asset Type Icons

| Extension | Icon label (ImGui text fallback) |
|-----------|----------------------------------|
| `.easset` | `[MESH]` |
| `.glb` / `.gltf` | `[GLB]` |
| `.scene` | `[SCENE]` |
| `.prefab` | `[PREFAB]` (Phase 8 Task #65) |

---

## 7. Mesh Preview

When the user clicks an `.easset` row in the Asset Browser (single-click, not drag), show a preview panel below the browser list (or in a floating tooltip after a 500 ms hover delay).

Preview panel contains:
- 64×64 thumbnail (from `ThumbnailRenderer`)
- Vertex count, triangle count (read from `.easset` header)
- Bounding box dimensions (AABB extents in metres)
- Material slot names
- Source file path (from `.meta`)
- **Reimport** button

---

## 8. Meta Sidecar File

Example `.easset.meta` on disk (TOML):

```toml
[source]
path        = "C:/art/character/hero.glb"
sha256      = "a3f8c1d9..."
mod_time    = 133900800000000000  # Windows FILETIME

[import_settings]
uniform_scale      = 1.0
up_axis            = "Y"
generate_collision = false
merge_meshes       = false
lod_count          = 1
```

The `.meta` file lives alongside the `.easset`:
```
assets/
  hero.easset
  hero.easset.meta
  .thumbnails/
    a3f8c1d9.png   ← keyed by source sha256
```

**Gitignore:** `.thumbnails/` should be added to `.gitignore` (regenerated on checkout).

---

## 9. Drag-Drop into Viewport

When the user drags an `.easset` from the Asset Browser and drops it onto the `ViewportPanel`:

1. Compute world-space drop position by raycasting from the drop pixel against the scene's static BVH; if no hit, place 5 m in front of the editor camera.
2. Call `EntityFactory::spawn("StaticPropEntity", SpawnParams{ .position = dropPos }, activeScene->world())`.
3. Set the spawned entity's mesh handle to the dropped `.easset`.
4. Set the `Collider` component if the `.easset.meta` has `generate_collision = true`.
5. Push a `CreateEntityCommand` onto the `UndoStack` so the drop is undoable.

---

## 10. Error Handling

| Failure | User-visible response |
|---------|----------------------|
| Source file is not valid glTF | Modal error dialog with `AssetImporter` error string |
| Source file has no meshes | Warning dialog; import still produces an empty `.easset` |
| Output `.easset` path already exists | Ask: Overwrite / Rename / Cancel |
| DX12 thumbnail render fails | Show `[MESH]` text icon; log `LOG_WARN`; do not block import |
| Disk full during write | Error dialog; clean up partial `.easset` before returning |

---

## 11. Tests

**File:** `tests/tools/EditorImporterTests.cpp` (label: unit)

- Import a known-good `.glb` fixture; verify `.easset` and `.meta` are written
- `MetaFileWriter::isStale()` returns false immediately after write, true after artificially bumping mtime
- `ImportSettings` round-trips through TOML correctly
- Reimport of the same file with different settings produces a fresh `.easset` and updates the `.meta`

No GPU tests for `ThumbnailRenderer` (skip with `GTEST_SKIP` when `!device.isValid()`).

---

## 12. Open Questions

- **Q:** Should `.glb` source files be copied into the project `assets/` folder on import, or left in-place (reference path in `.meta`)? Recommendation: leave in-place for Phase 8; add a "Collect Dependencies" wizard in Phase 9.
- **Q:** Should LOD generation be deferred entirely to Phase 9 or should the import dialog hide the LOD field? Recommendation: show the field, clamp to 1, log a `LOG_TRACE` noting the clamp.
