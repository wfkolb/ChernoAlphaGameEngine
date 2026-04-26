# Task #44 — EassetLoader

Status: Planned
Owner: Tools Lead
Phase: 5
References: Phase_5/README.md, tools-build-and-asset-pipeline.md, src/tools/AssetImporter.cpp

---

## 1. Purpose

`AssetImporter::importGltf()` writes `.easset` binary files to disk.
Nothing currently reads them back. This task adds `loadEasset()` — a reader
that turns a `.easset` file back into CPU-side geometry ready for GPU upload.

The consumer is Task #47 (SpinDemo), which calls `loadEasset` then passes
the result straight to `MeshManager::uploadStatic()`.

---

## 2. Binary Layout (matches AssetImporter.cpp)

The `.easset` format written by the importer is:

```
Offset  Size   Field
──────────────────────────────────────────────────────
0       20     EassHeader
20      16     TocEntry × tocEntryCount   (v1: always 1 entry, id = "MESH")
36      28     padding to next 64-byte boundary
64      36     MeshSectionHeader
100     28×N   VertexStatic array  (N = vertexCount)
100+28N 4×M    uint32_t index array (M = indexCount)
```

Struct definitions (packed, matches the writer exactly):

```cpp
struct EassHeader {
    char     magic[4];        // "EASS"
    uint16_t version;         // 1
    uint16_t assetType;       // 0 = Mesh
    uint32_t totalSize;
    uint32_t tocOffset;       // offset of first TocEntry from file start
    uint32_t tocEntryCount;
};  // 20 bytes

struct TocEntry {
    char     id[4];           // "MESH"
    uint32_t offset;          // offset of section from file start
    uint32_t size;            // section size in bytes
    uint32_t reserved;        // 0
};  // 16 bytes

struct MeshSectionHeader {
    uint32_t vertexCount;
    uint32_t indexCount;
    uint8_t  vertexLayout;    // 1 = kVertexLayoutStatic
    uint8_t  pad[3];
    float    aabbMin[3];
    float    aabbMax[3];
};  // 36 bytes
```

`VertexStatic` is already defined in `src/rendering/public/rendering/Mesh.h`:

```cpp
struct VertexStatic {
    float    position[3];   // 12 bytes
    uint32_t packedNormal;  //  4 bytes — R10G10B10A2_UNORM
    uint32_t packedTangent; //  4 bytes — R10G10B10A2_UNORM
    float    uv[2];         //  8 bytes
};  // 28 bytes total
```

---

## 3. Public API

### Header: `src/tools/public/tools/EassetLoader.h`

```cpp
#pragma once
#include <rendering/Mesh.h>
#include <filesystem>
#include <optional>
#include <vector>

namespace engine::tools {

struct CpuMesh {
    std::vector<rendering::VertexStatic> vertices;
    std::vector<uint32_t>               indices;
};

// Load a .easset file written by importGltf().
// Returns nullopt on any error: missing file, bad magic, version mismatch,
// unsupported asset type, truncated data.
std::optional<CpuMesh> loadEasset(const std::filesystem::path& path);

} // namespace engine::tools
```

### Implementation: `src/tools/EassetLoader.cpp`

Algorithm:

1. Open file in binary mode; read all bytes into a `std::vector<uint8_t>`.
2. Validate `fileBytes.size() >= sizeof(EassHeader)`.
3. Memcpy into `EassHeader`. Validate:
   - `magic == "EASS"`
   - `version == 1`
   - `assetType == 0`
   - `totalSize <= fileBytes.size()`
4. Find the `"MESH"` TocEntry by iterating `tocEntryCount` entries starting at `tocOffset`.
5. From the TocEntry `offset + size`, validate the mesh section fits in the file.
6. Memcpy `MeshSectionHeader` from `offset`.
7. Validate `vertexLayout == 1` (kVertexLayoutStatic).
8. Compute expected section size:
   ```
   sizeof(MeshSectionHeader)
   + vertexCount * sizeof(VertexStatic)
   + indexCount  * sizeof(uint32_t)
   ```
   Return `nullopt` if the TocEntry `size` is smaller.
9. Copy vertices: `std::memcpy(vertices.data(), ptr, vertexCount * sizeof(VertexStatic))`.
10. Copy indices: `std::memcpy(indices.data(), ptr, indexCount * sizeof(uint32_t))`.
11. Return `CpuMesh`.

No exceptions. All error paths return `nullopt`.

---

## 4. CMake

No CMake changes required. `src/tools/CMakeLists.txt` already uses:

```cmake
file(GLOB_RECURSE TOOLS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
```

`EassetLoader.cpp` is picked up automatically.

`EassetLoader.h` includes `<rendering/Mesh.h>`. The `engine_tools` target already
links `engine::core`, but check whether `engine::rendering` needs to be added as a
dependency (for `VertexStatic`). If `Mesh.h` is header-only with no link-time
symbols this may not be needed — confirm at build time.

---

## 5. Acceptance Criteria

- `loadEasset` on a file produced by `importGltf("dummy.glb", ...)` (unit-cube fallback)
  returns a `CpuMesh` with exactly **8 vertices** and **36 indices**.
- All vertex positions are within `[-0.5, 0.5]` on every axis.
- Passing a file with corrupted magic returns `std::nullopt` without crashing.
- Passing a non-existent path returns `std::nullopt`.
- No heap allocation when returning `nullopt`.

Unit tests for these criteria are in Task #48.
