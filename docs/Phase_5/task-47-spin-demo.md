# Task #47 — SpinDemo Application

Status: Planned
Owner: Team Leader
Phase: 5
References: Phase_5/README.md, task-44-easset-loader.md, task-46-flatshade-pass.md, src/rendering/Camera.h, src/core/math/Transform.h, src/core/math/Quat.h

---

## 1. Purpose

A standalone executable that:

1. Creates a window.
2. Imports a `.glb` file to a `.easset` file (or uses the built-in unit cube fallback).
3. Uploads the mesh to the GPU.
4. Renders the mesh spinning around the Y-axis with flat (normals-as-colour) shading.
5. Exits cleanly when the window closes.

This is the Phase 5 milestone deliverable — the first "you can see something 3D" moment.

---

## 2. File Location

`src/demos/SpinDemo.cpp`

No header needed — this is an application entry point, not a library.

The target is a standalone executable built by `src/demos/CMakeLists.txt`.

---

## 3. CMake

### New file: `src/demos/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.26)

file(GLOB DEMO_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(SpinDemo ${DEMO_SOURCES})

target_link_libraries(SpinDemo PRIVATE
    engine::rendering
    engine::tools
    engine::core)

# Copy compiled shaders to the SpinDemo output directory at build time
add_custom_command(TARGET SpinDemo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:SpinDemo>/shaders"
    COMMENT "Copying shaders to SpinDemo output")
```

### `src/CMakeLists.txt` — add subdirectory

Add to the bottom of `src/CMakeLists.txt`:

```cmake
add_subdirectory(demos)
```

---

## 4. Implementation: `src/demos/SpinDemo.cpp`

```cpp
#include <rendering/Window.h>
#include <rendering/GpuDevice.h>
#include <rendering/FrameGraph.h>
#include <rendering/MeshManager.h>
#include <rendering/Camera.h>
#include <rendering/FlatShadePass.h>
#include <rendering/internal/FlatShadePipeline.h>
#include <tools/EassetLoader.h>
#include <tools/AssetImporter.h>
#include <core/math/Mat.h>
#include <core/math/Quat.h>
#include <core/math/Transform.h>
#include <filesystem>
#include <cstdlib>

using namespace engine;
using namespace engine::core::math;
using namespace engine::rendering;
using namespace engine::tools;

int main(int argc, char** argv)
{
    // ── Asset ──────────────────────────────────────────────────────────────
    std::filesystem::path glbPath  = (argc > 1) ? argv[1] : "";
    std::filesystem::path eassetPath =
        std::filesystem::temp_directory_path() / "spindemo.easset";

    AssetImporter importer;
    importer.importGltf(glbPath, eassetPath);

    auto cpuMesh = loadEasset(eassetPath);
    if (!cpuMesh) {
        return EXIT_FAILURE;
    }

    // ── Window + Device ────────────────────────────────────────────────────
    Window::Desc wd;
    wd.title  = "SpinDemo";
    wd.width  = 1280;
    wd.height = 720;
    Window window = Window::create(wd);

    GpuDevice::Desc gd;
    gd.windowHandle = window.nativeHandle();
    gd.width        = wd.width;
    gd.height       = wd.height;
    GpuDevice device = GpuDevice::create(gd);
    if (!device.isValid()) {
        return EXIT_FAILURE;
    }

    // ── GPU resources ──────────────────────────────────────────────────────
    MeshManager meshMgr;
    meshMgr.uploadStatic(cpuMesh->vertices, cpuMesh->indices, device);

    constexpr uint32_t kBackFmt  = 87;   // DXGI_FORMAT_R8G8B8A8_UNORM
    constexpr uint32_t kDepthFmt = 20;   // DXGI_FORMAT_D32_FLOAT
    FlatShadePipeline flatPipeline =
        createFlatShadePipeline(
            static_cast<ID3D12Device*>(device.nativeDevice()),
            kBackFmt, kDepthFmt);

    FrameGraph fg;
    frameGraphSetDevice(fg, device.nativeDevice());

    // ── Camera ─────────────────────────────────────────────────────────────
    Camera cam;
    cam.fovY        = 3.14159265f / 3.0f;   // 60 degrees
    cam.nearZ       = 0.1f;
    cam.farZ        = 100.0f;

    Transform camTransform;
    camTransform.position = {0.0f, 1.5f, -4.0f};
    // Camera looks toward +Z (identity rotation, scene is in front)

    // ── Spin state ─────────────────────────────────────────────────────────
    float   angleRad   = 0.0f;
    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    // ── Main loop ──────────────────────────────────────────────────────────
    while (!window.wantsClose()) {
        window.pollEvents();

        QueryPerformanceCounter(&now);
        float dt = static_cast<float>(now.QuadPart - prev.QuadPart)
                   / static_cast<float>(freq.QuadPart);
        prev = now;
        dt = (dt > 0.1f) ? 0.1f : dt;   // clamp spike on first frame

        angleRad += dt * 1.0f;   // 1 radian/second

        // World matrix: rotate around Y
        Quat spinQ    = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, angleRad);
        Mat4 world    = spinQ.toMatrix();

        // View + Proj
        Mat4 view     = cameraViewMatrix(camTransform);
        float aspect  = static_cast<float>(wd.width) / static_cast<float>(wd.height);
        Mat4 proj     = cameraProjMatrix(cam, aspect);

        // MVP (row-vector convention: v' = v * M, so MVP = World * View * Proj)
        Mat4 mvp = world * view * proj;

        // ── Frame ──────────────────────────────────────────────────────────
        device.beginFrame();

        fg.reset();
        uint64_t bb = fg.importBackBuffer(device.nativeBackBuffer());
        uint64_t db = fg.importDepthBuffer(device.nativeDepthBuffer());

        addFlatShadePass(fg, meshMgr, flatPipeline, mvp.data(), bb, db);

        fg.compile();
        fg.execute(device.currentCommandList());

        device.endFrame();
    }

    device.flush();
    return EXIT_SUCCESS;
}
```

### Notes

**MVP construction:** row-vector convention means `posClip = posWorld * View * Proj`,
so the matrices multiply left-to-right. Pre-multiplied on CPU as `world * view * proj`
then passed as 16 floats to `SetGraphicsRoot32BitConstants`. The HLSL applies it as
`mul(float4(pos, 1.0), gMVP)`.

**`Quat::toMatrix()`:** returns a `Mat4` rotation. The `Quat` type has `toMatrix()`
per the math library design (see `src/core/math/Quat.h`).

**`device.nativeDepthBuffer()`:** returns the depth buffer as `void*`. Add this accessor
to `GpuDevice` if it does not already exist, matching the pattern for `nativeBackBuffer()`.

**`device.currentCommandList()`:** returns `void*` wrapping the open `ID3D12GraphicsCommandList*`
for the current frame. Add this accessor to `GpuDevice` if not present.

**Shader path:** `createFlatShadePipeline` loads `.cso` files. The post-build copy rule
in `CMakeLists.txt` ensures `shaders/FlatShadeVS.cso` and `shaders/FlatShadePS.cso`
sit beside the executable. `FlatShadePipeline.cpp` should search relative to the executable:
use `GetModuleFileNameW(NULL, ...)` or pass the shader directory as a parameter — follow
the same pattern as `PipelineState.cpp`.

**No ECS, no Engine.h:** SpinDemo is a direct-API demo. It does not instantiate `Engine`
or register any ECS components.

---

## 5. New GpuDevice Accessors (if missing)

If the following accessors are not already on `GpuDevice`, add them following the
existing `nativeBackBuffer()` pattern (internal header carries the impl, public header
declares the function, impl accesses `impl_->...`):

```cpp
// Returns the current frame's open ID3D12GraphicsCommandList* as void*.
void* currentCommandList() const noexcept;

// Returns the per-frame depth buffer ID3D12Resource* as void*.
void* nativeDepthBuffer() const noexcept;
```

Both return `nullptr` when `!impl_->valid`.

---

## 6. Acceptance Criteria

- SpinDemo compiles and links against `engine::rendering`, `engine::tools`, `engine::core`.
- Launched with no arguments: window opens, unit-cube mesh is visible spinning around Y,
  normal colours visible on all 6 faces.
- Launched with a valid `.glb` path: that model spins instead of the cube.
- Window close (× button) exits cleanly with no GPU hang or validation errors.
- DX12 debug layer produces zero errors and zero warnings during a 5-second run.
- `ctest -L unit` continues to pass 108/108 (SpinDemo is not a test target, it is
  an executable — it does not affect the unit test suite).
