# Coding Standards and Contribution Guidelines

Status: Approved (Phase 1)
Owner: Team Leader
Audience: Every contributor — leads and implementers.

This document is binding. CI enforces what can be enforced (clang-format, clang-tidy, warnings-as-errors); the rest is enforced in code review. If a rule here conflicts with a pre-existing comment in the codebase, this document wins; update the code.

---

## 1. Language and Toolchain

- **C++20**, MSVC v143 reference compiler. See `architecture.md` §1.
- `/W4 /WX` (warnings as errors). Suppressions live in `cmake/Warnings.cmake` with a one-line comment per suppression.
- `clang-format` (config in `.clang-format` at repo root) runs on commit via a pre-commit hook and in CI. CI rejects unformatted code.
- `clang-tidy` runs in CI with the project's `.clang-tidy` config. New checks are added by Team Leader.

## 2. File Layout

- One primary type per header. Helper types in the same logical area may share a header (e.g., `Vec2`, `Vec3`, `Vec4` may live in `Vec.h`).
- Header file extension: `.h`. Implementation: `.cpp`. Inline-template impls split out into `*.inl` and included at the bottom of the header.
- Public headers go under `src/<module>/public/<module>/`. Private headers go anywhere else under `src/<module>/`.

### Header order

Inside any `.cpp`, includes appear in this order, separated by blank lines:

1. The matching public/private header for this `.cpp` (if any).
2. Other headers from the same module.
3. Headers from other engine modules (`core/`, `tools/`, …).
4. Third-party headers (`<DirectXMath.h>`, `<imgui.h>`, …).
5. Windows SDK headers (`<windows.h>`, `<d3d12.h>`, …).
6. C++ standard library headers (`<vector>`, `<span>`, …).

Each group is sorted alphabetically. clang-format enforces grouping; sort order is by hand.

### Include guards

- **`#pragma once`** in every header. No traditional include guards.
- `#pragma once` lives on the line directly under the file's top-of-file comment (if any).

### Top-of-file comment

Optional. If present, one line, no copyright banners. Example:

```cpp
// Vec3.h — 3-component float vector with operator overloads. Header-only.
```

## 3. Naming Conventions

| Construct | Convention | Example |
|---|---|---|
| Types (class, struct, enum class, type alias) | `PascalCase` | `MeshRenderer`, `EntityHandle` |
| Methods, free functions | `camelCase` | `computeBounds()`, `submit()` |
| Local variables, parameters | `camelCase` | `int frameIndex` |
| Member variables | `camelCase` with trailing `_` | `int frameIndex_;` |
| Constants (`constexpr`, `const`-at-namespace, `enum class` enumerators) | `kPascalCase` | `kMaxLights`, `LogLevel::kError` |
| Macros (avoid; see §6) | `SCREAMING_SNAKE_CASE` with `ENGINE_` prefix | `ENGINE_ASSERT` |
| Namespaces | `lowercase` | `engine::core::math` |
| Files | match the dominant exported type | `MeshRenderer.h` |
| Template parameters | `PascalCase`, single capital `T`/`U` for trivial cases | `template<typename Component>` |

Notes:
- Trailing-underscore on members is a hard rule. `this->` is not used as a substitute.
- The leading `k` on constants applies to **named constants and enumerators**, not to `static constexpr` values that are obviously local (e.g., `for (int i = 0; ...)` — `i` is fine).
- Acronyms keep PascalCase casing of their first letter only: `GpuDevice`, not `GPUDevice`. `Hlsl`, not `HLSL`. (Consistent with DirectXMath conventions.)

## 4. Namespacing

- Top-level namespace: `engine`.
- Module namespaces: `engine::core`, `engine::rendering`, `engine::networking`, `engine::tools`, `engine::app`.
- Submodule namespaces are encouraged for clarity: `engine::core::math`, `engine::rendering::frame_graph`.
- **No** `using namespace` in headers. Inside a `.cpp`, `using namespace engine::core::math;` is allowed at function scope but discouraged at file scope.
- **No** anonymous-namespace types in public headers (they are TU-local).

## 5. Error Handling Strategy

The engine has **three** distinct error categories. Pick the right one; do not improvise.

### 5.1 Programming errors → `ENGINE_ASSERT`

Conditions that indicate a bug, not a user-input or environment problem.

```cpp
ENGINE_ASSERT(buffer != nullptr, "buffer must be initialized before draw()");
```

- Macro is defined in `core/diag/Assert.h`.
- In Debug and DevRel: prints, breaks into the debugger, and aborts.
- In Release: compiles to nothing for the condition (no side effects). The condition expression must be free of side effects.
- The message is a printf-style format string + args (uses `std::format` internally).

### 5.2 Recoverable errors → `Result<T, ErrorCode>` or `std::expected`-style

For operations that can fail on valid input (file not found, GPU device removed, network timeout).

```cpp
Result<Texture> LoadTexture(std::string_view path);

if (auto r = LoadTexture("missing.png"); !r) {
    LogError("texture load failed: {}", r.error());
    return;
}
```

- Defined in `core/diag/Result.h` (a thin wrapper around `std::expected` until our MSVC baseline guarantees it; v1 ships our own).
- Functions returning `Result<T>` must not throw.
- `[[nodiscard]]` is mandatory on every `Result<>`-returning function. clang-tidy enforces this.

### 5.3 Win32/DX12 HRESULT checks → `ENGINE_HR`

```cpp
ENGINE_HR(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue_)));
```

- Macro:
  - Asserts `SUCCEEDED(hr)` in Debug, breaking into the debugger on failure with the hr value, the call's source location, and the `IDXGIInfoQueue` last message.
  - In Release, logs the failure at `Error` level and converts to `Result<>` if the calling function returns one, or to a fatal abort if not.
- Use it for every HRESULT call. Bare `if (FAILED(hr))` is rejected in review.

### 5.4 Exceptions

- The engine is compiled with `/EHsc` but **does not throw** from its own code. Allocator OOM aborts; it does not throw.
- Standard library exceptions are caught **only** at well-defined boundaries: `WinMain` and the JSON/TOML config parser. Caught exceptions are logged and converted to `Result<>` or fatal abort.
- `noexcept` is added to functions that genuinely never throw (move constructors, dtors, swap, hashing). It is not sprinkled everywhere.

## 6. Macros

- **Avoid macros for code generation.** Use `constexpr`, templates, or generated headers.
- Approved macros, all defined under `core/`:
  - `ENGINE_ASSERT(cond, fmt, ...)`
  - `ENGINE_HR(call)`
  - `ENGINE_NO_COPY(Type)` and `ENGINE_NO_MOVE(Type)` — for `= delete` boilerplate.
  - `ENGINE_FALLTHROUGH` — `[[fallthrough]];` wrapper kept for old-toolchain compatibility (will be retired when MSVC baseline supports it cleanly).
  - `ENGINE_LIKELY(x)`, `ENGINE_UNLIKELY(x)` — wrap `[[likely]]`/`[[unlikely]]`.
  - Module-scoped logger macros (`LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, `LOG_FATAL`) — see §11.
- New macros require Team Leader sign-off in code review.
- Macros that are not in the approved list and that are TU-private must `#undef` themselves at the end of the file.

## 7. Memory Ownership

The engine's ownership model, in order of preference:

1. **Value types** (no owning pointer at all). Default for math, components, small POD-ish structs.
2. **`std::unique_ptr<T>`** for sole ownership across a boundary.
3. **`Handle<T>`** (defined in `core/memory`) — a generation-counted index into a pool. Use this anywhere you would have used a raw pointer that outlives the immediate scope.
4. **`std::shared_ptr<T>`** is **discouraged**. Allowed only when interfacing with third-party APIs (e.g., a few ImGui helpers) or for a small number of explicitly approved cases (asset cache entries shared with the renderer). Each new `shared_ptr<>` in engine code must be justified in review.
5. **Raw owning pointers** are forbidden in engine code. `new`/`delete` calls outside `core/memory` itself are forbidden. clang-tidy's `cppcoreguidelines-owning-memory` is enforced.
6. **Raw non-owning pointers and references** are fine for short-lived borrows. Lifetime must be clear from context (function parameter, member of an object whose lifetime is dominated by the pointee, etc.).

### Resource handles for OS/GPU resources

DX12 objects, Win32 handles, sockets, etc., are wrapped in RAII types in their owning module. Their move constructors transfer ownership; copy is deleted via `ENGINE_NO_COPY`.

### `const` discipline

- Method parameters that are not modified are `const T&` or `T` (when small).
- Methods that do not mutate state are `const`.
- `const` on local variables is encouraged but not enforced.
- `const` on by-value return types is forbidden (defeats move).

## 8. Types and Idioms

- Prefer `enum class` over `enum`. Underlying type is explicit when serialized: `enum class LogLevel : uint8_t { ... }`.
- Prefer fixed-width integers (`int32_t`, `uint64_t`) at module boundaries. Inside a function, `int` and `size_t` are fine.
- `auto` is encouraged when the type is obvious from the right-hand side (`auto it = map.find(...)`); discouraged when it obscures the type.
- `[[nodiscard]]` on return types whose value is the point of the call: `Result<>`, `bool` from query functions, allocator allocate calls.
- Prefer `std::span<T>` over `T*` + `size_t` parameter pairs.
- Prefer `std::string_view` for non-owning string parameters.
- Prefer designated initializers for aggregate types: `D3D12_RESOURCE_DESC desc{ .Dimension = ..., .Width = ... };`. (DX12 structs love this pattern.)
- Prefer `if (auto x = ...; cond(x))` to scope variables tightly.
- `const auto&` over `const auto` when the source is a container element of non-trivial type.

## 9. Class Design

- Public, then protected, then private. One `public:`/`private:` block of each, not interleaved.
- Member order within a class:
  1. Public type aliases and nested types.
  2. Public constructors, destructor, copy/move.
  3. Public methods.
  4. Public data members (rare — usually only for POD-ish structs).
  5. Private nested types.
  6. Private methods.
  7. Private data members.
- Special members: declare or `= default` / `= delete` all five (default ctor, copy ctor, copy assign, move ctor, move assign) when any one is non-trivial. Use `ENGINE_NO_COPY` / `ENGINE_NO_MOVE` for the common shapes.
- Trivial getters/setters live in the header inline. Anything else goes in the `.cpp`.

## 10. Comments

- Default to no comment. Code that needs a comment to be readable usually needs a better name first.
- A comment is justified when it explains **why**, not what: an invariant, a workaround for a known driver bug (link the bug), a non-obvious performance choice.
- No comments referencing the current task, ticket, or PR. ("Fixes #142" lives in the commit message, not in `// fix for #142` in source.)
- Doc-comments on public APIs: a one-line summary above the declaration, plus parameter/return notes only when non-obvious. Doxygen tags are not used; we don't generate API docs.
- `// TODO(name):` is allowed and must include a name and either a ticket reference or a one-line reason. `// TODO` alone is rejected in review.

## 11. Logging

```cpp
LOG_INFO("loaded mesh '{}' ({} verts, {} indices)", path, vertCount, idxCount);
```

- Macros: `LOG_TRACE`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, `LOG_FATAL`. Format strings use `std::format` syntax.
- One log macro family, defined in `core/log.h`. Tools provides the implementation (`tools/Logger`).
- `LOG_FATAL` calls `std::abort` after flushing.
- No `printf`/`std::cout` in engine code. Tests may use `std::cout` but should prefer `LOG_*` to stay consistent with engine output.
- Logs at `Trace` are stripped in Release (compile-time elision).

## 12. Threading

- Single-threaded by default in v1. Document any code that is intended to be reentrant or callable from a non-main thread.
- Locks: `std::mutex` for general use, `std::shared_mutex` for read-heavy state. SRWLock wrappers in `core/threading` for hot paths.
- A function that holds a lock must not call into another module's public API while holding it (deadlock prevention rule of thumb).
- Lock ordering, when a function holds two locks, is documented in `core/threading/LockOrder.md` (added when the second lock pair appears).

## 13. Performance Conventions

- Don't optimize without a measurement. Premature `[[likely]]`, manual loop unrolling, or hand-written SIMD outside `core/math` is rejected in review.
- Math hot paths: it is acceptable (and expected) to write SIMD in `core/math`; isolate it behind the value-type API and provide a scalar fallback for non-AVX2 builds (kept primarily for unit tests).
- Allocations in hot frame paths: forbidden. `Update`, `LateUpdate`, and the render submission loop must not call into the global allocator. Use the `frameArena` from ECS or a per-system arena.
- Containers: prefer `FixedVector<T, N>` from `core/containers` when an upper bound is known. Use `std::vector` otherwise.

## 14. Tests

- Every new public function in `core` ships with a unit test in the same PR.
- Test files mirror the source layout: `src/core/math/Vec3.h` → `tests/core/math/Vec3Tests.cpp`.
- Use Google Test fixtures (`TEST_F`) for setup-heavy suites. Use `TEST` for one-shot cases.
- Tests must be deterministic. Random-input tests use a seeded `std::mt19937` and log the seed.

## 15. Git, Branches, and PRs

- Branch from `main`. Branch names: `<area>/<short-slug>`, e.g., `rendering/swapchain-init`, `core/math-quat`.
- Commits: imperative present tense (`add quaternion slerp`), under 72 chars in the subject line. Reference task IDs in the body, not the subject (`Refs: task #17`).
- One logical change per commit. Squash on merge.
- PRs are reviewed by the lead of the affected module. Cross-module PRs require both leads.
- CI must be green before merge: build (Debug + Release), unit tests, clang-format, clang-tidy.
- Force-pushing to `main` is forbidden. Force-pushing to feature branches is fine.

## 16. Review Checklist (for reviewers)

When reviewing a PR, confirm at minimum:

- [ ] Builds on Debug and Release in CI.
- [ ] No new warnings.
- [ ] Public headers respect the boundary rules in `module-structure.md` §4.
- [ ] Naming and layout match this document.
- [ ] No raw `new`/`delete`, no smart-pointer abuse.
- [ ] Tests added for new public API in core.
- [ ] No TODO without a name and reason.
- [ ] Commit messages are clean.

## 17. What's NOT a rule

To avoid bikeshedding:

- Brace style: clang-format decides. Don't argue.
- Blank-line counts: clang-format decides.
- Trailing-return-type vs. leading: contributor's choice; be consistent within a file.
- East-const vs. west-const: clang-format does not enforce; we use **west-const** (`const T&`) by convention but reviewers do not block on it.
