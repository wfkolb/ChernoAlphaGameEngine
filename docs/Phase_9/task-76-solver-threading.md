# Task #76 — TD-06: Physics Solver Threading

**Phase 9 — physics / core — Version 0.9.x**
**Audience:** Physics developer
**Severity:** 🟠 High — required before multiplayer beta (64-player stress test)
**Depends on:** Phase 7 #52 (PhysicsWorld), new `TaskScheduler` in `engine::core`

---

## 1. Goal

`PhysicsWorld::step()` currently runs all phases (broad-phase, narrow-phase, constraint solve) on the game thread. At 64 Hz with 64 players and ~200 dynamic bodies, solver time will exceed the 15.6 ms budget on typical dev hardware. This task parallelises the independent parts of the pipeline using a minimal task scheduler added to `engine::core`.

---

## 2. Current State

`PhysicsWorld::step()` is fully sequential:
1. Integrate velocities
2. Broad-phase: update dynamic grid, collect candidate pairs
3. Narrow-phase: SAT/GJK/EPA per pair → contact manifolds
4. Island detection: group connected bodies
5. Constraint solve: iterative impulse per island
6. Integrate positions

Steps 3 (narrow-phase) and 5 (constraint solve per independent island) are parallelisable. Steps 1, 2, 4, 6 must remain sequential (data dependencies).

---

## 3. TaskScheduler (new, engine::core)

A minimal work-stealing thread pool added to `engine::core` for use by the physics solver (and later the animation system). This does **not** use Windows thread pool APIs — it's a simple fixed-size pool to keep the implementation portable.

**File:** `src/core/public/core/task/TaskScheduler.h`

```cpp
namespace engine::core {

class TaskScheduler {
public:
    explicit TaskScheduler(uint32_t threadCount = 0);  // 0 = hardware_concurrency - 1
    ~TaskScheduler();

    ENGINE_NO_COPY(TaskScheduler);
    ENGINE_NO_MOVE(TaskScheduler);

    // Submit a batch of tasks and wait for all to complete.
    // Fn = void(uint32_t index, uint32_t total)
    template<typename Fn>
    void parallelFor(uint32_t count, Fn&& fn);

    uint32_t threadCount() const noexcept;
};

} // namespace engine::core
```

`parallelFor` blocks until all items complete. No futures, no heap allocation for simple lambdas (use `std::function` with small-buffer optimisation, or a fixed-size task array).

`TaskScheduler` is owned by `PhysicsWorld`. One instance per physics world. It is constructed in `PhysicsWorld::PhysicsWorld()` using `hardware_concurrency() - 1` threads (leave one for the game thread).

---

## 4. Parallel Narrow-Phase

Candidate pairs from broad-phase are independent — the SAT/GJK/EPA test for pair (A, B) does not affect the test for pair (C, D). Parallelise:

```cpp
// Sequential (current):
for (const auto& pair : broadPhasePairs_) {
    narrowPhase_.test(pair, contacts_);
}

// Parallel (new):
const uint32_t N = static_cast<uint32_t>(broadPhasePairs_.size());
scheduler_.parallelFor(N, [&](uint32_t i, uint32_t) {
    narrowPhase_.testSingle(broadPhasePairs_[i], localContacts_[i]);
});
// Merge localContacts_ into contacts_ (sequential, fast)
```

`NarrowPhase::testSingle()` must be const / stateless — it reads the body positions (read-only during this phase) and writes to a per-task output buffer. No shared mutable state. Verify this is true before parallelising.

---

## 5. Parallel Constraint Solve

After island detection, independent islands (no shared bodies) can be solved in parallel:

```cpp
scheduler_.parallelFor(static_cast<uint32_t>(islands_.size()),
    [&](uint32_t i, uint32_t) {
        solver_.solveIsland(islands_[i]);
    });
```

`solver_.solveIsland()` must only read/write the bodies in its own island. This is true if island detection is correct (no body appears in two islands). Add an `ENGINE_ASSERT` in debug builds that verifies this invariant.

---

## 6. Thread Safety Audit

Before enabling parallelism, audit these shared resources:

| Resource | Access pattern | Action required |
|----------|---------------|-----------------|
| `bodies_` map | Read during narrow-phase, written during integrate | Safe: phases are sequential |
| `broadPhasePairs_` vector | Read-only during parallel narrow-phase | Safe |
| `contacts_` | Written during narrow-phase | **Unsafe** — use per-thread buffers, merge after |
| `EventBus` trigger events | Written during step | **Unsafe** — queue per-thread, flush after |
| `LOG_*` macros | Multiple threads | Safe if Logger uses a lock (verify) |

---

## 7. Files to Create / Modify

| File | Change |
|------|--------|
| `src/core/public/core/task/TaskScheduler.h` | New — parallel-for scheduler |
| `src/core/task/TaskScheduler.cpp` | New — thread pool implementation |
| `src/core/CMakeLists.txt` | Add task/ sources |
| `src/physics/PhysicsWorld.h` | Add `TaskScheduler scheduler_` member |
| `src/physics/PhysicsWorld.cpp` | Parallelise narrow-phase and solver |
| `src/physics/internal/NarrowPhase.h/.cpp` | Add `testSingle()` (stateless per-pair) |

---

## 8. Tests

**File:** `tests/physics/SolverThreadingTests.cpp` (label: unit)

- `TaskScheduler` with 4 threads: submit 100 tasks; verify all 100 run and counter == 100.
- Parallel narrow-phase: 50 pairs, identical results to sequential run (determinism).
- Parallel island solve: 3 independent islands; verify same final positions as sequential solve.
- Race condition: run 1000 parallel-for iterations that all increment a `std::atomic<int>`; verify == 1000.

---

## 9. Known Issues

- **Determinism:** Floating-point operations in parallel are not guaranteed to produce the same result as sequential (different accumulation order). Physics simulation will be deterministic only if pairs are processed in a fixed order. For Phase 9, accept minor non-determinism in the solve (affects visual smoothness, not gameplay correctness). Fully deterministic parallel physics is a Phase 10 goal.
- **Single-threaded fallback:** If `hardware_concurrency() == 1`, `TaskScheduler` runs all tasks on the calling thread (no threads spawned). This makes `parallelFor` equivalent to a sequential loop and keeps the single-threaded test runner working correctly.
