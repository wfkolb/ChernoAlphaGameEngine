# Task #48 — Phase 5 Tests

Status: Planned
Owner: Test Lead
Phase: 5
References: Phase_5/README.md, task-44-easset-loader.md, task-47-spin-demo.md, tests/tools/AssetPipelineTest.cpp

---

## 1. Purpose

Two test files covering the new Phase 5 code:

1. **`EassetLoaderTest.cpp`** — unit tests for `loadEasset()`. These are
   `ctest -L unit` tests: no GPU, no window, no network.

2. **`SpinDemoTest.cpp`** — a headless crash-guard that verifies the SpinDemo
   main-loop logic compiles and doesn't blow up when there is no display.
   Labelled `integration` so it is skipped by `ctest -L unit`.

Both files follow existing test conventions in `tests/tools/` and `tests/rendering/`.

---

## 2. EassetLoaderTest.cpp

### Location

`tests/tools/EassetLoaderTest.cpp`

Picked up automatically by the existing `GLOB_RECURSE` in `tests/tools/CMakeLists.txt`
(same mechanism that already builds `AssetPipelineTest.cpp`).

### Tests

```cpp
#include <gtest/gtest.h>
#include <tools/EassetLoader.h>
#include <tools/AssetImporter.h>
#include <filesystem>
#include <fstream>
#include <cstring>

using namespace engine::tools;
namespace fs = std::filesystem;

// ── Fixture ──────────────────────────────────────────────────────────────────

class EassetLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = fs::temp_directory_path() / "test_easset_loader.easset";
        AssetImporter imp;
        imp.importGltf("", path_);   // empty source → unit-cube fallback
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove(path_, ec);
    }
    fs::path path_;
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(EassetLoaderTest, UnitCubeHas8VerticesAnd36Indices) {
    auto mesh = loadEasset(path_);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->vertices.size(), 8u);
    EXPECT_EQ(mesh->indices.size(),  36u);
}

TEST_F(EassetLoaderTest, VertexPositionsWithinUnitHalfExtent) {
    auto mesh = loadEasset(path_);
    ASSERT_TRUE(mesh.has_value());
    for (const auto& v : mesh->vertices) {
        EXPECT_GE(v.position[0], -0.5f);  EXPECT_LE(v.position[0], 0.5f);
        EXPECT_GE(v.position[1], -0.5f);  EXPECT_LE(v.position[1], 0.5f);
        EXPECT_GE(v.position[2], -0.5f);  EXPECT_LE(v.position[2], 0.5f);
    }
}

TEST_F(EassetLoaderTest, CorruptMagicReturnsNullopt) {
    // Overwrite magic bytes in the temp file
    {
        std::fstream f(path_, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(f.is_open());
        f.seekp(0);
        f.write("XXXX", 4);
    }
    auto mesh = loadEasset(path_);
    EXPECT_FALSE(mesh.has_value());
}

TEST_F(EassetLoaderTest, NonExistentPathReturnsNullopt) {
    auto mesh = loadEasset(fs::temp_directory_path() / "does_not_exist_xyz.easset");
    EXPECT_FALSE(mesh.has_value());
}

TEST_F(EassetLoaderTest, TruncatedFileReturnsNullopt) {
    // Read file, write back with last 100 bytes removed
    std::vector<uint8_t> bytes;
    {
        std::ifstream f(path_, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(f), {});
    }
    if (bytes.size() > 100) {
        bytes.resize(bytes.size() - 100);
        std::ofstream f(path_, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    auto mesh = loadEasset(path_);
    EXPECT_FALSE(mesh.has_value());
}

TEST_F(EassetLoaderTest, WrongVersionReturnsNullopt) {
    // Patch version field (bytes 4–5, uint16_t) to 99
    {
        std::fstream f(path_, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(f.is_open());
        f.seekp(4);
        uint16_t badVer = 99;
        f.write(reinterpret_cast<char*>(&badVer), sizeof(badVer));
    }
    auto mesh = loadEasset(path_);
    EXPECT_FALSE(mesh.has_value());
}
```

All tests are labelled `unit` (inherited from the target's `LABELS` property in
`tests/tools/CMakeLists.txt`). No GPU required, no network, no window. Each test
cleans up its temp file in `TearDown`.

---

## 3. SpinDemoTest.cpp

### Location

`tests/demos/SpinDemoTest.cpp`

### New CMakeLists: `tests/demos/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.26)

add_executable(spin_demo_tests SpinDemoTest.cpp)

target_link_libraries(spin_demo_tests PRIVATE
    engine::rendering
    engine::tools
    engine::core
    GTest::gtest_main)

add_test(NAME SpinDemoHeadlessTest COMMAND spin_demo_tests)
set_tests_properties(SpinDemoHeadlessTest PROPERTIES LABELS "integration")
```

Add to `tests/CMakeLists.txt`:

```cmake
add_subdirectory(demos)
```

### Tests

```cpp
#include <gtest/gtest.h>
#include <rendering/Window.h>
#include <rendering/GpuDevice.h>
#include <rendering/FrameGraph.h>
#include <rendering/MeshManager.h>
#include <rendering/Camera.h>
#include <rendering/FlatShadePass.h>
#include <rendering/internal/FlatShadePipeline.h>
#include <tools/EassetLoader.h>
#include <tools/AssetImporter.h>
#include <core/math/Quat.h>
#include <core/math/Transform.h>
#include <filesystem>

using namespace engine;
using namespace engine::core::math;
using namespace engine::rendering;
using namespace engine::tools;

// Verify the asset load + GPU-upload path doesn't crash headlessly.
TEST(SpinDemoTest, AssetLoadAndUploadHeadless) {
    namespace fs = std::filesystem;

    fs::path easset = fs::temp_directory_path() / "spindemo_test.easset";
    {
        AssetImporter imp;
        imp.importGltf("", easset);
    }
    auto mesh = loadEasset(easset);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->vertices.size(), 8u);
    EXPECT_EQ(mesh->indices.size(),  36u);

    std::error_code ec;
    fs::remove(easset, ec);
}

// Verify GpuDevice creation soft-fails gracefully with no display.
// Mirrors SmokeTest.cpp's GTEST_SKIP pattern.
TEST(SpinDemoTest, GpuDeviceSkipsGracefullyHeadless) {
    Window::Desc wd;
    wd.title  = "SpinDemoTestWindow";
    wd.width  = 1280;
    wd.height = 720;
    Window window = Window::create(wd);

    GpuDevice::Desc gd;
    gd.windowHandle = window.nativeHandle();
    gd.width        = wd.width;
    gd.height       = wd.height;
    GpuDevice device = GpuDevice::create(gd);

    if (!device.isValid()) {
        GTEST_SKIP() << "GpuDevice swapchain failed (headless/no display)";
    }

    // If we get here, we have a real device. Verify a basic frame cycle.
    device.beginFrame();
    device.endFrame();
    device.flush();
}

// Verify MVP math doesn't produce NaN/Inf for a unit spin.
TEST(SpinDemoTest, MvpIsFiniteAfterOneSpin) {
    Quat q    = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 1.0f);
    Mat4 world = q.toMatrix();

    Transform camT;
    camT.position = {0.0f, 1.5f, -4.0f};

    Camera cam;
    cam.fovY  = 3.14159265f / 3.0f;
    cam.nearZ = 0.1f;
    cam.farZ  = 100.0f;

    Mat4 view = cameraViewMatrix(camT);
    Mat4 proj = cameraProjMatrix(cam, 16.0f / 9.0f);
    Mat4 mvp  = world * view * proj;

    for (int i = 0; i < 16; ++i) {
        EXPECT_TRUE(std::isfinite(mvp.data()[i])) << "mvp[" << i << "] is not finite";
    }
}
```

The third test (`MvpIsFiniteAfterOneSpin`) is pure math — no GPU, no window,
runs under `ctest -L unit` if labelled appropriately. To allow that, split it
into its own file or use a separate target:

### Splitting test labels

Option A (simpler): keep all three tests in `SpinDemoTest.cpp` but set the target
label to `integration`. The MVP test will only run under `ctest -L integration`.

Option B (preferred): add a second file `SpinDemoMathTest.cpp` with only
`MvpIsFiniteAfterOneSpin`, link it into the `spin_demo_tests` target but register
it under a separate `ctest` test name with label `unit`. This lets `ctest -L unit`
catch regressions in the MVP math.

The Test Lead should choose Option B and add:

```cmake
add_test(NAME SpinDemoMathTest COMMAND spin_demo_tests --gtest_filter=SpinDemoTest.MvpIsFiniteAfterOneSpin)
set_tests_properties(SpinDemoMathTest PROPERTIES LABELS "unit")
```

This increases the unit test count from 108 to **109**.

---

## 4. Acceptance Criteria

- `EassetLoaderTest` — 6 tests, all pass under `ctest -L unit`.
  - `UnitCubeHas8VerticesAnd36Indices` ✓
  - `VertexPositionsWithinUnitHalfExtent` ✓
  - `CorruptMagicReturnsNullopt` ✓
  - `NonExistentPathReturnsNullopt` ✓
  - `TruncatedFileReturnsNullopt` ✓
  - `WrongVersionReturnsNullopt` ✓

- `SpinDemoTest.MvpIsFiniteAfterOneSpin` — passes under `ctest -L unit` (109th unit test).

- `SpinDemoTest.GpuDeviceSkipsGracefullyHeadless` — passes (skips) under `ctest -L integration`
  on a headless machine; passes with a real result on a machine with a display.

- `ctest -L unit`: **109/109** pass after this task (up from 108).
- No new DX12 types in public headers introduced by test code.
- Temp files cleaned up — no leftover `.easset` files after test run.
