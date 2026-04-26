# Scope: Test Lead

Status: Approved (Phase 1)
Owner: Team Leader (this doc); Test Lead (the work it scopes).
References: `architecture.md`, `module-structure.md`, `ecs-design.md`, `coding-standards.md`.

This is the binding scope for the Test Lead through Phases 2–4. The Test Lead's mandate is broad: testing infrastructure, CI, and benchmarks across all four modules.

---

## What you own

The `engine/tests/` tree and `engine/tests/benchmarks/`, plus the testing-side of CI (Tools Lead owns the runner config; you own the matrix and the test invocations).

In particular:

1. **Google Test 1.14+ harness** — common test main, fixture base classes, custom matchers shared across modules.
2. **Per-module test targets** — `core_tests`, `rendering_tests`, `networking_tests`, `tools_tests` and the conventions for adding tests under each.
3. **CI test matrix** — Debug and Release, Win10 runner and Win11 runner, with each module's tests broken out as separate steps so failures are easy to pinpoint.
4. **Math library and ECS unit tests** — the foundation tests; these are the canonical examples for other leads to copy.
5. **Rendering smoke test** — engine starts headless (or with a hidden window), creates the DX12 device, clears a render target, reads it back, asserts pixel correctness.
6. **Networking loopback test** — server + client in one process exchange a packet, validate transport correctness end-to-end.
7. **Performance benchmarks** — Google Benchmark targets that run nightly and store baselines.
8. **Phase 4 integration:** full test suite + benchmark baselines in task #43.

## What you do NOT own

- **Per-module unit tests beyond #36 and #37.** Each module's lead is responsible for adding tests for new code in their module. You provide the harness, examples, and conventions; they write the tests.
- **The GitHub Actions runner setup.** Tools Lead owns the workflow YAML and runner labels.
- **The CMake `enable_testing()` and `gtest_discover_tests` plumbing.** Tools Lead owns CMake; you tell them what test discovery you need.
- **Code coverage instrumentation.** Out of v1 scope. Document for v2.
- **Static analysis tools beyond clang-tidy.** clang-tidy is owned by Tools Lead; PVS-Studio / Coverity / etc. are out of scope.
- **Fuzzing infrastructure.** Networking Lead's fuzz harness for the packet serializer (task #28) is owned by them; you make sure CI runs it.
- **The actual gameplay code under test.** v1 has no shipping gameplay; you test the engine.

## Dependencies on other modules

| You depend on | For | Owner |
|---|---|---|
| Tools Lead's CMake | `enable_testing()`, `gtest_discover_tests`, CTest invocation | Tools Lead, task #31 |
| Tools Lead's CI workflow | The runner where your tests execute | Tools Lead |
| Each module's public headers | What the tests can `#include` | Each lead |
| Each module's lead | Domain-specific assertions and test fixtures (e.g., a "with-DX12-device" fixture for rendering) | Each lead |
| `core::math` (#17) | Subject-of-test for the math suite | Team Leader |
| `core::ecs` (#19) | Subject-of-test for the ECS suite | Team Leader |
| `engine_rendering`'s context init (#22) | Smoke test entry | Rendering Lead |
| `engine_networking`'s socket wrapper (#27) | Loopback test entry | Networking Lead |

## Phase 2 deliverable (scope/design doc)

| Task | Deliverable | Required content |
|---|---|---|
| #16 | `docs/testing-plan.md` | Framework choice (already decided: GTest 1.14+); test target structure (one per module); naming conventions for test files (`Vec3Tests.cpp`, `WorldTests.cpp`, etc.); CI matrix (Debug + Release × Win10 + Win11); flake budget (zero — re-runs are evidence of a real bug); guidance for writing deterministic rendering tests (golden-image comparison with tolerance, where the goldens live, how to update them); guidance for networking tests (real loopback over `127.0.0.1`, no mocked sockets — see "no mocking the database" feedback culture); benchmark structure and baseline storage. |

Reviewed and approved by the Team Leader before implementation tasks (#35–38) start.

## Phase 3 deliverables (code)

In ID order: tasks #35, #36, #37, #38.

Definitions of done:

- **#35 — Google Test harness and CI pipeline.** Each of `core_tests`/`rendering_tests`/`networking_tests`/`tools_tests` builds, links, and runs at least a sanity test (`TEST(Sanity, OnePlusOne)`). CI runs all four targets in Debug and Release on Windows. Failed tests block PR merge.
- **#36 — Math and ECS unit tests.** Vec/Mat/Quat operator algebra, transform composition, slerp, intersection helpers; Entity stable handles across archetype moves, component add/remove, view iteration with optional/excluded components, command buffer ordering. Coverage target: every public function in `core::math` and `core::ecs` exercised by at least one test.
- **#37 — Rendering smoke test and networking loopback test.** Rendering: device init → clear color → readback → assert. Run headless if possible, hidden window if not. Networking: in-process server + client, ping/pong over UDP loopback, assert packet integrity and timing.
- **#38 — Benchmarks with recorded baselines.** Google Benchmark suite covering at minimum: math hot paths (Mat4 multiply, Quat slerp, frustum culling), ECS view iteration over 10k entities across 3 components, packet serialization for a 1k-entity snapshot, logger throughput, asset cook time. Baselines stored in `tests/benchmarks/baselines/` as JSON and committed; CI runs the suite and warns (does not fail) if a result regresses by >15%.

## Test infrastructure conventions

- **Test files mirror source layout.** `src/core/math/Vec3.h` → `tests/core/math/Vec3Tests.cpp`. CI fails if a test file lives outside the mirrored path.
- **Naming:** `TEST_F(Vec3Test, addsComponentwise)` — fixture name `<Type>Test`, case name verb-phrase in camelCase.
- **No mocking of OS primitives.** No fake sockets, no fake file system, no fake DX12 device. Use the real thing on a real loopback / temp directory / actual device. Rationale (carry-over from the team's "don't mock the database" culture): mocked tests pass and prod fails.
- **Determinism is non-negotiable.** A test that fails 1 in 1000 runs is a real bug, not a flake. CI re-runs are not used to paper over flakes.
- **Test isolation.** Each test sets up and tears down its own state. Static globals shared between tests are forbidden. The logger is the documented exception (process-global), and tests must not assert on log content unless using a captured-log fixture.
- **Random-input tests** seed `std::mt19937` from a known constant in the test, log the seed at start, and use a CLI flag to override the seed for reproduction.
- **Golden images** for rendering tests live under `tests/rendering/goldens/`, are PNG, and tolerate ≤ 2% per-channel RMSE. Updating a golden requires a 2-line PR description explaining why. The compare tool lives in `tests/rendering/support/`.

## Benchmark conventions

- **One benchmark target per module:** `core_bench`, `rendering_bench`, `networking_bench`, `tools_bench`.
- **Baselines** are JSON, committed under `tests/benchmarks/baselines/<host-class>/`. v1 has one host class: `desktop-mid` (define the spec in the testing plan). CI uploads run results as artifacts and compares against the matching host-class baseline.
- **No asserting on absolute timings in PR CI.** Hardware variance makes that a flake source. Use it for trend monitoring.
- **Microbenchmarks should be hostile.** Pin to a core, disable turbo if possible (document if not), warm caches, and report `cycles_per_op` as the headline number.

## CI matrix (target shape)

```
Build:
  - {os: windows-2022, config: Debug,   runner: github-hosted}
  - {os: windows-2022, config: Release, runner: github-hosted}
  - {os: windows-11,   config: Debug,   runner: self-hosted}    [optional, gated]
  - {os: windows-11,   config: Release, runner: self-hosted}    [optional, gated]

Steps per build:
  1. checkout
  2. cmake configure with vcpkg manifest install
  3. cmake build
  4. ctest -L unit (core_tests, tools_tests)
  5. ctest -L integration (rendering_tests, networking_tests — needs GPU, runner-gated)
  6. clang-format check (Tools)
  7. clang-tidy check (Tools)
  8. (nightly only) ctest -L benchmark, archive JSON results
```

The labels `unit`, `integration`, and `benchmark` are set on test targets via `set_tests_properties(... PROPERTIES LABELS ...)`.

## Performance targets (test infra)

| Metric | Target |
|---|---|
| `core_tests` runtime | ≤ 30 s |
| Full unit-test suite (all modules) | ≤ 3 min on a github-hosted runner |
| Smoke + loopback integration tests | ≤ 1 min total |
| Benchmark suite (nightly) | ≤ 10 min |

These targets are not gates; they are budgets. Going substantially over indicates a problem worth investigating.

## Communication

- Coordinate with Tools Lead on the CI workflow file: you supply the test invocations and label filters; they wire the runner.
- Coordinate with Rendering Lead on the headless/hidden-window plumbing for the smoke test — that may require a small renderer-side feature.
- Coordinate with Networking Lead on the loopback test fixture — they may want to expose a helper that constructs a server and client in one process.
