# Testing Plan

Status: Approved (Phase 2)
Owner: Test Lead
Task: #16
References: architecture.md §9, scope-testing.md, coding-standards.md §14

---

## 1. Framework

**Google Test 1.14+** for all unit and integration tests. **Google Benchmark** for all microbenchmarks. Both sourced via vcpkg (tools-build-and-asset-pipeline.md §5).

No other testing framework is used in v1. No mocking library (gmock) — tests use real implementations.

---

## 2. Test Target Structure

One CMake test target per module. Test targets link to the module under test, `engine::core`, and `GTest::gtest_main`.

| Target | Module under test | Label | Notes |
|---|---|---|---|
| `core_tests` | `engine::core` | `unit` | Math, ECS, containers, memory, events |
| `rendering_tests` | `engine::rendering` | `integration` | Requires real DX12 device; GPU runner only |
| `networking_tests` | `engine::networking` | `integration` | Real loopback UDP; any runner |
| `tools_tests` | `engine::tools` | `unit` | Logger, config, asset format (file I/O) |

Benchmark targets:

| Target | Label |
|---|---|
| `core_bench` | `benchmark` |
| `rendering_bench` | `benchmark` |
| `networking_bench` | `benchmark` |
| `tools_bench` | `benchmark` |

Labels are set via `set_tests_properties(... PROPERTIES LABELS ...)`. CI uses `-L unit` for PR builds and `-L integration` on GPU runners.

---

## 3. Test File Naming and Location

Test files mirror the source layout exactly:

```
src/core/math/Vec.h            → tests/core/math/VecTests.cpp
src/core/ecs/World.h           → tests/core/ecs/WorldTests.cpp
src/networking/Serializer.h    → tests/networking/SerializerTests.cpp
src/tools/Logger.h             → tests/tools/LoggerTests.cpp
```

CI fails if a test file path does not mirror a source path under `tests/<module>/`.

Test file naming: `<TypeName>Tests.cpp`. For free-function groups (e.g., math utilities): `<feature>Tests.cpp` (e.g., `IntersectTests.cpp`).

---

## 4. Naming Conventions

```cpp
// Fixture: <TypeName>Test
class Vec3Test : public ::testing::Test { ... };

// Test cases: TEST_F(FixtureName, verbPhraseInCamelCase)
TEST_F(Vec3Test, addsComponentwise) { ... }
TEST_F(Vec3Test, normalizesUnitVector) { ... }
TEST_F(Vec3Test, handlesZeroLengthNormalize) { ... }

// One-shot tests: TEST(SuiteName, caseName)
TEST(MathConstants, piIsCorrect) { ... }
```

Test names describe a behavior, not an implementation: `normalizesUnitVector` not `callsNormalizeFunction`.

---

## 5. Core Rules

### 5.1 No mocking of OS primitives

No mock sockets, no fake file system, no fake DX12 device.

- **Networking tests** use real UDP loopback over `127.0.0.1`. Server and client run in the same process (see task #29 — `Session::createLocalPair()`).
- **Rendering tests** use a real hidden window and real DX12 device. If the test runner has no GPU, the test is skipped via `GTEST_SKIP()` after a device-creation probe.
- **File system tests** use the OS's temp directory (`std::filesystem::temp_directory_path()`). Tests clean up after themselves in `TearDown`.
- **Logger tests** capture log output via a test sink registered before the test, not by mocking `gLogFn`.

Rationale: mocked tests have historically passed while production builds broke. Real implementations find real bugs.

### 5.2 Determinism is mandatory

A test that fails 1 in 1000 runs is a real bug, not a flake. CI does not retry failing tests.

- Random-input tests: seed `std::mt19937` from a known constant (e.g., `42`). Log the seed at `TEST_F::SetUp()`. Provide a `--gtest_filter=... --seed=N` mechanism for reproduction (CLI argument parsed in `TestMain`).
- Time-dependent tests: use `core::time::FixedTimestep` with a fixed `dt`, never `std::chrono::system_clock`.
- Network timing: tests do not assert on latency; they assert on correctness. Loopback is always < 1 ms in practice.

### 5.3 Test isolation

Each test constructs and destructs its own state. No shared global state between tests (the logger is the documented exception — it is process-global but tests must not assert on log content unless using a `CapturedLogFixture`).

```cpp
class CapturedLogFixture : public ::testing::Test {
protected:
    void SetUp() override {
        captured_.clear();
        core::log::gLogFn = [](const LogEntry& e) { captured_.push_back(e); };
    }
    void TearDown() override {
        core::log::gLogFn = tools::Logger::dispatch;  // restore
    }
    static std::vector<LogEntry> captured_;
};
```

### 5.4 No assertions on absolute performance in PR CI

Benchmarks run nightly. PR CI runs the unit/integration test suite, not benchmarks. This avoids hardware-variance-induced flakes in PR builds.

---

## 6. CI Matrix

```yaml
# PR CI (runs on every PR):
matrix:
  os: [windows-2022]
  config: [Debug, Release]
steps:
  1. cmake configure + build
  2. ctest -L unit          # core_tests, tools_tests
  3. ctest -L integration   # networking_tests (loopback; no GPU needed)
  4. clang-format check
  5. clang-tidy check

# GPU-gated (self-hosted runner with GPU; runs on PR if labeled "needs-gpu"):
steps:
  1. cmake configure + build (Release only)
  2. ctest -L integration   # rendering_tests (real DX12 device)

# Nightly (self-hosted desktop-mid runner):
steps:
  1. cmake configure + build Release
  2. ctest -L benchmark
  3. upload benchmark JSON as artifact
  4. compare against baseline; warn (do not fail) if >15% regression
```

The "self-hosted desktop-mid" hardware spec (for baseline comparisons):
- CPU: 8-core desktop, 3.5 GHz+ (e.g., Ryzen 7 or Core i7, no overclock)
- GPU: mid-range discrete (RTX 3060 or equivalent VRAM)
- RAM: 16 GB
- OS: Windows 11 22H2

---

## 7. Math and ECS Test Coverage (task #36)

### 7.1 Math tests (`tests/core/math/`)

Every public function in `core::math` is exercised by at least one test.

Required test cases per type:

**Vec3Tests.cpp:**
- `addsComponentwise`, `subtractsComponentwise`, `scalesUniform`, `computesDot`, `computesCross`
- `normalizesUnitVector`, `handlesZeroLengthNormalize` (returns zero vec)
- `lerpsCorrectly`, `distancesCorrectly`, `reflectsAcrossNormal`
- `staticFactoriesAreCorrect` (zero, one, unitX, etc.)

**QuatTests.cpp:**
- `identityMulIsIdentity`, `mulIsAssociative`, `conjugateInvertsRotation`
- `fromAxisAngleRoundTrips`, `slerpInterpolatesCorrectly`
- `slerpAtTZeroIsA`, `slerpAtTOneIsB`
- `toMat4RoundTrips`, `fromMat4RoundTrips`
- `normalizesCorrectly`, `handlesNearZeroNormalize`

**Mat4Tests.cpp:**
- `identityMulIsIdentity`, `mulIsAssociative`, `transposeSwapsElements`
- `inverseOfIdentityIsIdentity`, `mulByInverseIsIdentity`
- `perspectiveMapsFarToZero` (reverse-Z: far → 0.0, near → 1.0)
- `lookAtProducesCorrectBasis`
- `translationMovesByVec`, `scalingScalesByVec`

**TransformTests.cpp:**
- `defaultTransformIsIdentity`
- `toMatrixAndBackRoundTrips`
- `composeParentChildIsCorrect`

### 7.2 ECS tests (`tests/core/ecs/`)

**WorldTests.cpp:**
- `createEntityReturnsAlive`
- `destroyedEntityIsNotAlive`
- `stableHandleAcrossArchetypeMove` — add component to entity; check handle still valid after move
- `addRemoveComponentCycleCorrect`
- `getReturnsNullForMissingComponent`
- `getReturnsNullForDestroyedEntity`

**ViewTests.cpp:**
- `viewIteratesMatchingArchetypes`
- `viewExcludesExcludedComponent`
- `viewOptionalReturnsNullForMissingComponent`
- `viewIterationOrderIsConsistent` (same order across multiple calls same frame)
- `viewIsInvalidatedByNewArchetype` (cache miss detected, not UB)

**CommandBufferTests.cpp:**
- `deferredAddComponentAppliedAfterFlush`
- `deferredDestroyEntityAppliedAfterFlush`
- `commandOrderIsPreservedWithinFlush`

---

## 8. Rendering Smoke Test (task #37)

```cpp
// tests/rendering/SmokeTest.cpp
TEST(RenderingSmoke, deviceInitClearAndReadback) {
    // Skip if no DX12 device is available
    if (!GpuDevice::isAvailable()) { GTEST_SKIP() << "No DX12 device"; }

    auto window = Window::create({.width=64, .height=64, .title=L"Test"});
    // ShowWindow with SW_HIDE — window is not visible
    auto device = GpuDevice::create({.window=&window, .vsync=false});

    // One frame: clear to known color
    device.beginFrame();
    // ... set up frame graph: clear to {1, 0, 0, 1} (red), present
    device.endFrame();
    device.flush();

    // Readback via a copy to a readback heap
    auto pixels = readbackBackBuffer(device, 0, 0, 64, 64);
    // Assert first pixel is approximately red
    EXPECT_NEAR(pixels[0].r, 255, 2);
    EXPECT_NEAR(pixels[0].g,   0, 2);
    EXPECT_NEAR(pixels[0].b,   0, 2);
}
```

The `readbackBackBuffer` helper allocates a readback heap, copies the swapchain back buffer to it, and returns pixel data. It is a test-support utility in `tests/rendering/support/ReadbackHelper.h`.

The window is created with `SW_HIDE`; the test runs headless. If the DX12 device is unavailable (CI runner without GPU), the test is skipped — not failed.

---

## 9. Networking Loopback Test (task #37)

```cpp
// tests/networking/LoopbackTest.cpp
TEST(NetworkingLoopback, pingPongRoundTrip) {
    auto [server, client] = Session::createLocalPair(/*port=*/17777);
    // Both server and client run in this process, bound to 127.0.0.1

    bool serverReceived = false;
    server.onMessage([&](const Endpoint&, std::span<const uint8_t> data) {
        EXPECT_EQ(data.size(), 4u);
        EXPECT_EQ(data[0], 0xDE);
        serverReceived = true;
        // echo back
        server.send(client.localEndpoint(), data);
    });

    bool clientReceived = false;
    client.onMessage([&](const Endpoint&, std::span<const uint8_t> data) {
        EXPECT_EQ(data.size(), 4u);
        EXPECT_EQ(data[0], 0xDE);
        clientReceived = true;
    });

    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    client.send(server.localEndpoint(), payload);

    // Poll both for up to 100 ms
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (!clientReceived && std::chrono::steady_clock::now() < deadline) {
        server.poll(); client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(serverReceived);
    EXPECT_TRUE(clientReceived);
}
```

`Session::createLocalPair()` is a test-support factory in `networking/Session.h` (gated by `#ifdef ENGINE_TESTING` or similar). Coordinate with the Networking Lead on the exact API.

---

## 10. Benchmark Structure (task #38)

### 10.1 Baseline storage

```
tests/benchmarks/baselines/
└── desktop-mid/
    ├── core_bench.json
    ├── rendering_bench.json
    ├── networking_bench.json
    └── tools_bench.json
```

JSON format: Google Benchmark's native `--benchmark_format=json` output. Files are committed to git on the first baseline run and updated manually when intentional performance changes land.

### 10.2 Required benchmarks

**`core_bench`:**
```cpp
BM_Mat4Multiply         // 10M iterations; measures Mat4 * Mat4
BM_QuatSlerp            // 10M iterations; measures slerp(a, b, 0.5)
BM_FrustumCull_10k      // Frustum::test() on 10k AABBs, random
BM_EcsViewIterate_10k   // View<Transform, Velocity> over 10k entities, 3 archetypes
```

**`networking_bench`:**
```cpp
BM_SnapshotEncode_1k    // Encode snapshot for 1000 entities with Transform+Velocity
BM_SnapshotDecode_1k    // Decode same snapshot
BM_SerializerBitWrite   // BitWriter, 1000 × writeU32
BM_QuatSmallestThree    // Encode + decode 1M quaternions
```

**`tools_bench`:**
```cpp
BM_LoggerThroughput     // LOG_INFO rate, single thread, 100k messages
BM_AssetCook_Simple     // Cook a small glTF (< 1000 triangles, 1 texture)
```

**`rendering_bench`** (GPU runner only; skip if no device):
```cpp
BM_FrameGraphCompile    // compile a 5-pass frame graph
BM_DrawCallRecord_1k    // record 1000 draw calls on the command list
```

### 10.3 Benchmark conventions

- Pin to one core: `benchmark::DoNotOptimize`, `benchmark::ClobberMemory`.
- Report `cycles_per_op` as the headline metric using `BENCHMARK_MAIN()` with the PMU counter flag if available; otherwise fall back to `ns/op`.
- Warm the cache with one un-timed iteration at the start of each benchmark (`state.SkipWithError` if warmup fails).
- No assertions on absolute timing in PR CI. Nightly compares against the committed baseline and posts a warning comment if any benchmark regresses by > 15%.

---

## 11. Golden Image Comparison (rendering)

Rendering integration tests that verify pixel output use golden images stored under `tests/rendering/goldens/`.

- Format: PNG, RGBA8, dimensions matching the test's hidden window.
- Tolerance: per-channel RMSE ≤ 2% (i.e., ≤ 5.1 / 255 RMS difference per channel).
- Comparison tool: `tests/rendering/support/GoldenCompare.cpp` — reads two PNGs (via `stb_image`), computes per-channel RMSE, fails if over threshold.
- Updating a golden: run the test with `--update-goldens` flag; the test writes the new golden. Requires a 2-line PR description explaining why the visual output changed.
- Goldens are committed to git (small files; toolchain differences between machines may require per-runner goldens in the future — deferred to v2).
