# Tools: Build System and Asset Pipeline

Status: Approved (Phase 2)
Owner: Tools Lead
Task: #13
References: architecture.md §5, module-structure.md §6, scope-tools.md

---

## 1. Top-Level CMake Structure

```
engine/
├── CMakeLists.txt              ← top-level (Tools Lead + Team Leader co-own)
├── cmake/
│   ├── Warnings.cmake          ← /W4 /WX + suppression list
│   ├── ShaderCompile.cmake     ← DXC invocation rule
│   ├── AssetCook.cmake         ← .gltf → .easset rule
│   ├── VcpkgHelpers.cmake      ← vcpkg toolchain integration helpers
│   └── ClangTidy.cmake         ← clang-tidy integration
└── src/
    ├── core/CMakeLists.txt
    ├── rendering/CMakeLists.txt
    ├── networking/CMakeLists.txt
    ├── tools/CMakeLists.txt
    └── app/CMakeLists.txt
```

### 1.1 Top-level CMakeLists.txt responsibilities

```cmake
cmake_minimum_required(VERSION 3.26)
project(Engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# vcpkg integration (toolchain file set via preset or -DCMAKE_TOOLCHAIN_FILE)
include(cmake/VcpkgHelpers.cmake)

# Warnings
include(cmake/Warnings.cmake)

# Module dependency guard (enforces the allowed link graph from module-structure.md §2.1)
include(cmake/ModuleDepCheck.cmake)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/rendering)
add_subdirectory(src/networking)
add_subdirectory(src/tools)
add_subdirectory(src/app)
add_subdirectory(tests)
add_subdirectory(samples EXCLUDE_FROM_ALL)
```

---

## 2. CMake Presets

`CMakePresets.json` lives at `engine/CMakePresets.json`. Three configure presets:

| Preset | CMAKE_BUILD_TYPE | Extra defines | Description |
|---|---|---|---|
| `debug` | `Debug` | `ENGINE_DEBUG=1` | Full debug, GPU validation, no optimization |
| `devrel` | `RelWithDebInfo` | `ENGINE_DEVREL=1` | Optimized, assertions on, PIX markers, shader hot-reload |
| `release` | `Release` | — | Fully optimized, no assertions, no debug layer |

Build presets mirror configure presets: `build-debug`, `build-devrel`, `build-release`.

Test preset: `test-debug` (runs `ctest -C Debug --output-on-failure`), `test-release`.

Usage:
```
cmake --preset debug -S engine -B build/debug
cmake --build --preset build-debug
ctest --preset test-debug
```

---

## 3. Warnings Configuration (`cmake/Warnings.cmake`)

```cmake
# Applied to every engine target via target_compile_options
set(ENGINE_COMPILE_FLAGS
    /W4           # Warning level 4
    /WX           # Warnings as errors
    /permissive-  # Strict conformance
    /Zc:__cplusplus  # Report correct __cplusplus value
    /Zc:preprocessor # New conforming preprocessor
    /wd4201       # nonstandard extension: nameless struct/union (used by DX headers)
    # Additional suppressions documented below with rationale:
)
# Each suppression MUST have a comment explaining why it is needed.
```

Apply via a CMake function:
```cmake
function(engine_add_warnings target)
    target_compile_options(${target} PRIVATE ${ENGINE_COMPILE_FLAGS})
endfunction()
```

Called from each module's `CMakeLists.txt`. The top-level `CMakeLists.txt` does not apply warnings globally — only to engine targets.

---

## 4. Module CMakeLists.txt Pattern

Each module follows the same pattern (example: `core`):

```cmake
# src/core/CMakeLists.txt
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "*.cpp")
file(GLOB_RECURSE CORE_HEADERS CONFIGURE_DEPENDS "public/**/*.h")

add_library(engine_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
add_library(engine::core ALIAS engine_core)

target_include_directories(engine_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal
)
target_compile_features(engine_core PUBLIC cxx_std_20)
engine_add_warnings(engine_core)

# PCH (per module)
target_precompile_headers(engine_core PRIVATE internal/pch.h)
```

Allowed link edges (module-structure.md §2.1) are the only `target_link_libraries` calls. The `ModuleDepCheck.cmake` script runs a configure-time check that no target links a disallowed peer module.

---

## 5. vcpkg Manifest (`engine/vcpkg.json`)

```json
{
  "name": "engine",
  "version": "0.1.0",
  "dependencies": [
    "directx-headers",
    "directxtk12",
    "winpixeventruntime",
    "meshoptimizer",
    "imgui",
    "gtest",
    "benchmark",
    "tomlplusplus"
  ],
  "overrides": [],
  "builtin-baseline": "<current vcpkg commit hash>"
}
```

`stb` and `dxc` are vendored (not in vcpkg), per architecture.md §5. They live under `engine/third_party/`.

Adding a new dependency requires:
1. Team Leader approval.
2. A `vcpkg.json` PR entry.
3. A comment in `vcpkg.json` listing the purpose.

`vcpkg` is configured in manifest mode; the toolchain file is `[vcpkg-root]/scripts/buildsystems/vcpkg.cmake`, set via a preset variable `CMAKE_TOOLCHAIN_FILE`.

---

## 6. Shader Compile Rule (`cmake/ShaderCompile.cmake`)

### 6.1 DXC invocation

```cmake
function(engine_compile_shader target shader_file entry_point profile output_name)
    set(output_dxv "${CMAKE_CURRENT_BINARY_DIR}/shaders/${output_name}")
    set(dep_file   "${CMAKE_CURRENT_BINARY_DIR}/shaders/${output_name}.d")

    add_custom_command(
        OUTPUT  ${output_dxv}
        COMMAND ${DXC_EXECUTABLE}
            -T ${profile}             # e.g. vs_6_6 or ps_6_6
            -E ${entry_point}
            -Fo ${output_dxv}
            -I  ${ENGINE_SHADER_DIR}  # engine/shaders
            -WX -Ges
            -Qstrip_reflect
            -O3
            -MD -MF ${dep_file}       # dependency tracking
            $<$<CONFIG:Debug>:-Od -Zi>
            $<$<CONFIG:RelWithDebInfo>:-Od -Zi -Fd ${output_dxv}.pdb>
            ${shader_file}
        DEPENDS ${shader_file}
        DEPFILE ${dep_file}
        COMMENT "Compiling shader ${shader_file}"
        VERBATIM
    )
    target_sources(${target} PRIVATE ${output_dxv})
endfunction()
```

`DXC_EXECUTABLE` is resolved by `VcpkgHelpers.cmake` from the vendored DXC binary under `third_party/dxc/`.

Touching a `.hlsl` or `.hlsli` file causes only the affected shader to recompile (the `-MD` dependency file tracks `#include` chains).

### 6.2 Registering shaders

In `src/rendering/CMakeLists.txt`:
```cmake
engine_compile_shader(engine_rendering
    ${CMAKE_SOURCE_DIR}/shaders/opaque/OpaqueVS.hlsl VSMain vs_6_6 OpaqueVS.dxv)
engine_compile_shader(engine_rendering
    ${CMAKE_SOURCE_DIR}/shaders/opaque/OpaquePS.hlsl PSMain ps_6_6 OpaquePS.dxp)
# ... etc.
```

Compiled bytecode files are installed alongside the executable in the `shaders/` output directory.

---

## 7. Asset Cooking Rule (`cmake/AssetCook.cmake`)

### 7.1 Cooker executable

The asset cooker is a separate CMake executable target `asset_cooker` built from `engine_tools`. It is NOT linked into the runtime `engine` executable.

```cmake
add_executable(asset_cooker src/tools/asset_cooker/main.cpp)
target_link_libraries(asset_cooker PRIVATE engine::tools engine::core)
```

### 7.2 Cook rule

```cmake
function(engine_cook_asset target source_asset output_easset)
    add_custom_command(
        OUTPUT  ${output_easset}
        COMMAND $<TARGET_FILE:asset_cooker>
            --input  ${source_asset}
            --output ${output_easset}
        DEPENDS ${source_asset} asset_cooker
        COMMENT "Cooking asset ${source_asset}"
        VERBATIM
    )
    target_sources(${target} PRIVATE ${output_easset})
endfunction()
```

Usage:
```cmake
engine_cook_asset(engine
    ${CMAKE_SOURCE_DIR}/assets/source/helmet/helmet.gltf
    ${CMAKE_BINARY_DIR}/assets/cooked/helmet.easset)
```

Touching `helmet.gltf` or any referenced texture causes only that asset to be recooked. The cooker is deterministic given the same input (no timestamps in output files).

---

## 8. `.easset` Format Specification

### 8.1 Binary layout

```
Offset  Size  Field
0       4     magic = "EASS"
4       2     version = 1
6       2     assetType (0=Mesh, 1=Texture, 2=Material, 3=Pack)
8       4     totalSize (bytes, including header)
12      4     tocOffset (offset to TOC from start of file)
16      4     tocEntryCount
--- TOC entries (at tocOffset) ---
Per entry (16 bytes):
  0  4   sectionId  (FourCC, e.g. "MESH", "MTRL", "TEX0".."TEX9", "MIPS")
  4  4   offset     (from start of file)
  8  4   size       (bytes)
  12 4   reserved   (set to 0)
--- Section data ---
```

All values little-endian. The TOC follows the fixed 20-byte header. Section data follows the TOC. All sections are 64-byte aligned (pad with zeros to alignment).

### 8.2 Mesh section ("MESH")

```cpp
struct MeshSectionHeader {
    uint32_t vertexCount;
    uint32_t indexCount;
    uint8_t  vertexLayout;   // 0=Position, 1=Static, 2=Skinned
    uint8_t  pad[3];
    float    aabbMin[3];
    float    aabbMax[3];
};
// Followed by: vertex data (vertexCount * sizeof(vertexLayout))
// Followed by: index data  (indexCount * sizeof(uint32_t))
```

### 8.3 Material section ("MTRL")

Raw `GpuMaterial` struct (from rendering-mesh-material-shader.md §6.1). One struct per material in the file.

### 8.4 Texture section ("TEX0"–"TEX9", "MIPS")

```cpp
struct TextureSectionHeader {
    uint32_t width, height;
    uint32_t mipCount;
    uint32_t dxgiFormat;     // DXGI_FORMAT value
    uint32_t arraySize;      // 1 = 2D, 6 = cube
};
// Followed by mip data for each mip level, top-down (largest first)
// Each mip is aligned to 256 bytes (DX12 texture data alignment requirement)
```

The runtime loads texture data zero-copy via `core::fs::MemoryMappedFile` and passes the pointer directly to `ID3D12Resource::CopyFrom`.

### 8.5 Versioning

- Bump `version` when any section layout changes incompatibly.
- The runtime checks `version` at load time and refuses to load unknown versions (`LOG_ERROR` + return `Result::Err`).
- The C++ struct definitions in `tools/AssetWriter.h` are the authoritative spec. The markdown above is documentation only.

---

## 9. Install Layout

```
install/
├── engine.exe
├── engine.toml               ← default config (installed from source)
├── shaders/                  ← compiled .dxv / .dxp files
│   └── ...
├── assets/
│   └── cooked/               ← .easset files
│       └── ...
└── third_party/dxc/
    └── dxcompiler.dll        ← required for DevRel shader hot-reload
```

CMake install rules live in the top-level `CMakeLists.txt` under an `install(...)` block. The `dxcompiler.dll` is installed only for Debug and DevRel configurations.

---

## 10. CI Matrix

```yaml
# .github/workflows/build.yml (skeleton)
jobs:
  build:
    strategy:
      matrix:
        os: [windows-2022]
        config: [Debug, Release]
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake --preset ${{ matrix.config == 'Debug' && 'debug' || 'release' }} -S engine -B build
      - name: Build
        run: cmake --build build --config ${{ matrix.config }}
      - name: Unit tests
        run: ctest --test-dir build -C ${{ matrix.config }} -L unit --output-on-failure
      - name: clang-format check
        run: python scripts/check_format.py
      - name: clang-tidy
        run: cmake --build build --target clang-tidy
```

Integration tests (rendering, networking) are gated on a self-hosted GPU runner — they do not run on the github-hosted runner. The Test Lead defines the step; the Tools Lead wires the runner label.

Nightly-only:
```yaml
  benchmark:
    runs-on: [self-hosted, desktop-mid]
    steps:
      - run: ctest --test-dir build -C Release -L benchmark --output-on-failure
      - uses: actions/upload-artifact@v4
        with: { name: bench-results, path: tests/benchmarks/baselines/ }
```
