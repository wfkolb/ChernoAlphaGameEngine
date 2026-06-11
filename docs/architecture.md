# Engine Architecture and Technology Decisions

Status: Approved (Phase 1)
Owner: Team Leader
Audience: All four leads (rendering, networking, tools, test) and core engine implementers.

This document records the binding technology decisions for the Windows 3D game engine. All other documents (module structure, ECS design, coding standards, scope docs) build on these choices.

---

## 1. Target Platform

- **Primary platform:** Windows 10 22H2 and Windows 11 (x64).
- **Secondary platform:** none for v1. The engine intentionally avoids cross-platform abstractions to keep the rendering and networking layers thin.
- **Compiler:** MSVC v143 (Visual Studio 2022) as the reference toolchain. Clang-cl is allowed for local development but is not required to pass CI.
- **C++ standard:** C++20. Features in active use: concepts, `std::span`, `<bit>`, designated initializers, `consteval`, `[[likely]]/[[unlikely]]`, `std::format`. Modules are NOT used in v1 (toolchain still rough).
- **Minimum CPU baseline:** SSE4.2 + AVX2. The math library is allowed to assume AVX2 intrinsics.

## 2. Graphics API

- **Decision: DirectX 12 (DX12) is the only graphics backend in v1.**

### Rationale

| Option | Pros | Cons | Verdict |
|---|---|---|---|
| DX12 | Native to Windows; explicit pipeline state, command lists, fences; modern bindless via descriptor heaps; first-class tooling (PIX). | Verbose initialization; explicit lifetime management. | **Chosen.** |
| Vulkan | Cross-platform; explicit. | Adds an extra abstraction surface; Windows tooling weaker than PIX; no cross-platform requirement in v1. | Rejected. |
| DX11 | Simpler API. | Driver-managed state machine; poor multi-threaded command recording; not the long-term target. | Rejected. |
| OpenGL | Mature. | Deprecated direction; legacy state-machine model. | Rejected. |

### DX12 specifics adopted

- Feature level **12_1** required, **12_2** (DXR 1.1, mesh shaders) preferred. The renderer probes capabilities at startup and disables optional passes if unsupported.
- **Shader model 6.6** as the compile target; 6.5 as the floor.
- Shader compiler: **DXC** (`dxcompiler.dll`) only. FXC is not used.
- Debug layer (`ID3D12Debug`, GPU-based validation) is enabled in Debug and DevRel builds, off in Release.
- PIX runtime hooks compiled in for DevRel.

## 3. Windowing and Input

- **Win32 only.** No SDL, no GLFW. The engine owns its message pump.
- Window creation lives in `engine/src/rendering` (the renderer owns the swapchain, so it owns the HWND lifetime), but raw input distribution lives in `engine/src/core` via the event bus.
- Raw Input (`WM_INPUT`) is the source of truth for mouse and keyboard. Legacy `WM_KEYDOWN` is used only for IME and accelerator-style edge cases.
- Gamepad input via XInput 1.4. DirectInput is not supported.

## 4. Networking

- **Winsock2** (`ws2_32.lib`) directly. No external networking middleware (no ENet, no RakNet, no asio in v1).
- Transport: **UDP** as the primary game-loop transport; a thin reliability layer (sequence numbers, acks, optional resends) is layered on top. TCP is only used for initial matchmaking handshake.
- IPv4 and IPv6 dual-stack sockets.
- Packet framing is custom (see `docs/scope-networking.md`).

## 5. Build System and Toolchain

- **CMake 3.26+** is the single source of truth. Visual Studio solutions are generated, never hand-authored.
- **Ninja** is the preferred local generator; the Visual Studio generator is supported but not required for CI.
- **vcpkg in manifest mode** (`vcpkg.json`) for third-party dependencies. The manifest is checked in; the binary cache is not.
- Compile-commands export (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`) for clangd/IDE tooling.
- Warning level `/W4` with `/WX` (warnings as errors). A small, explicitly-named warning suppression list lives in `cmake/Warnings.cmake`; new suppressions require a comment explaining why.

### Approved third-party dependencies (v1)

| Library | Purpose | Source |
|---|---|---|
| DirectXMath / DirectX-Headers | DX12 helpers, math fallback | vcpkg |
| DXC (`directx-dxc`) | HLSL compilation | vcpkg (host tool) |
| WinPixEventRuntime | PIX markers | vcpkg |
| stb | Texture decode (`stb_image.h`) and mip resize (`stb_image_resize2.h`) | vcpkg |
| meshoptimizer | Mesh import optimization | vcpkg |
| cgltf | glTF 2.0 parsing (header-only) | vcpkg |
| Dear ImGui (docking branch) | Editor UI | vcpkg |
| ImGuizmo | Editor gizmo (translate/rotate/scale overlay) | vcpkg |
| Google Test 1.14+ | Unit and integration tests | vcpkg |
| Google Benchmark | Microbenchmarks | vcpkg |
| toml++ | TOML configuration parsing | vcpkg |
| lz4 | Data compression | vcpkg |

Anything not on this list requires a written exception approved by the Team Leader before being added to `vcpkg.json`.

## 6. Math Library

- **Decision: Custom math library lives in `engine/src/core/math`.**

### Rationale

DirectXMath is excellent for SIMD intrinsics but its `XMVECTOR` SoA-leaning API leaks SIMD types into headers and makes the public API awkward for game code. GLM is column-major and HLSL-friendly but pulls a lot of templates and has historical quirks on MSVC.

The custom library:
- Wraps `XMVECTOR`/`XMMATRIX` internally for the hot paths (transform composition, frustum culling, skinning).
- Exposes a clean value-type API: `Vec2/Vec3/Vec4`, `Mat3/Mat4`, `Quat`, with row-major storage and right-handed coordinates.
- Coordinate convention: **right-handed, Y-up, +Z forward.** Same convention as the renderer, networking quaternion serialization, and the editor.
- Is header-only for trivial types; non-trivial routines (decompose, slerp tables, etc.) live in `.cpp`.

GLM is allowed inside `engine/src/tools` (asset importer) for convenience but must be converted at module boundaries. GLM types must not appear in core or rendering public headers.

## 7. Memory and Threading

- **Allocator strategy:** the global `new`/`delete` operators are not overridden in v1. Subsystems that need bulk allocation (rendering frame data, ECS component storage, asset loading) own arena and pool allocators from `core/memory` (task #18).
- **Threading model:** main thread owns the message pump and the simulation tick. A worker pool (job system) is planned for v2; for v1 the renderer may use up to two threads (main + GPU submission) and the asset importer runs synchronously.
- **Synchronization primitives:** `std::mutex`, `std::shared_mutex`, `std::atomic`. SRWLock is acceptable for hot paths but must be wrapped in `core/threading`.

## 8. Logging, Configuration, Assets

- **Logging:** custom logger in `engine/src/tools/logger`. Outputs to console (with VT-100 color) and to a rotating file in `%LOCALAPPDATA%/<engine>/logs/`. No spdlog or fmt dependency — uses `std::format`.
- **Configuration:** TOML via `toml++` (vcpkg). Config files live alongside the executable as `engine.toml`, with optional user overrides in `%APPDATA%/<engine>/`.
- **Asset format:** importer (`tools/asset_importer`) consumes glTF 2.0 for meshes and PNG/HDR for textures, and emits a packed binary `.easset` runtime format (little-endian). Runtime never parses glTF directly.

## 9. Testing

- **Google Test 1.14+** for unit and integration tests.
- **Google Benchmark** for microbenchmarks.
- One test target per module: `core_tests`, `rendering_tests`, `networking_tests`, `tools_tests`.
- CI runs via GitHub Actions (`.github/workflows/ci.yml`), matrix over Debug and Release on `windows-2022`. Unit tests are blocking; integration and GPU tests run on a self-hosted runner with `continue-on-error`. See `docs/scope-testing.md` for details.

## 10. Versioning and Branching

- `main` is always green. All work flows in via PRs from short-lived feature branches.
- Semantic version starts at `0.1.0`. The first running engine in Phase 4 is tagged `0.1.0`.
- No public API guarantees in v0.x.

## 11. Out of scope for v1

The following are explicitly **not** part of v1 and must not be designed around:

- macOS / Linux / console support
- Vulkan or DX11 fallback
- Hot-reloadable scripting (Lua, AngelScript, etc.)
- DLSS/FSR/XeSS
- Ray tracing as a primary path (DXR 1.1 may be probed for future use only)

---

## Decision Log

| Date | Decision | Rationale |
|---|---|---|
| 2026-04-24 | DX12 sole backend | Windows-only target; PIX tooling; modern explicit API. |
| 2026-04-24 | C++20, MSVC v143 reference | Concepts + `std::format` are load-bearing in core APIs. |
| 2026-04-24 | Custom math library, RH/Y-up | Avoids leaking SIMD types; consistent with renderer + serializer. |
| 2026-04-24 | CMake + vcpkg manifest | Reproducible builds; no checked-in third-party source for libraries vcpkg can supply. |
| 2026-04-24 | Winsock2 directly | No middleware; UDP + custom reliability layer. |
| 2026-06-07 | Phase 10 Wave 1 | XInput gamepad, clock abstraction, texture pipeline, skinned mesh stub, CI added. |
