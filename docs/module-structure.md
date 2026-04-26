# Module Structure and Directory Layout

Status: Approved (Phase 1)
Owner: Team Leader

This document defines the top-level layout of the engine repository, which subsystems live in which module, and what the public API boundaries are between them. It is binding for all leads.

---

## 1. Top-Level Directory Layout

```
engine/
├── CMakeLists.txt              # Top-level: declares project, options, subdirectories
├── vcpkg.json                  # Manifest dependencies
├── cmake/                      # Reusable CMake helpers (Warnings.cmake, ShaderCompile.cmake, etc.)
├── docs/                       # Architecture, scope, and design docs (this file lives here)
├── third_party/                # Vendored sources (stb, dxc binaries) — vcpkg deps NOT here
├── assets/                     # Source assets (glTF, PNG, HDR) and the cooked asset cache
│   ├── source/                 # Authoring inputs (committed)
│   └── cooked/                 # Build output (gitignored)
├── shaders/                    # HLSL source — owned by rendering, compiled by build
├── src/
│   ├── core/                   # Module: core
│   ├── rendering/              # Module: rendering
│   ├── networking/             # Module: networking
│   ├── tools/                  # Module: tools (logger, config, asset importer, editor)
│   └── app/                    # Module: app — the executable that wires modules together
├── tests/
│   ├── core/                   # core_tests target
│   ├── rendering/              # rendering_tests target
│   ├── networking/             # networking_tests target
│   ├── tools/                  # tools_tests target
│   └── benchmarks/             # Google Benchmark targets
└── samples/                    # Small executables that exercise a single subsystem
```

Rules:

- All authored code lives under `engine/src/`. Tests live under `engine/tests/`. No test code in `src/`.
- `engine/third_party/` is reserved for sources that are not available through vcpkg (see `architecture.md` §5). Adding to it requires Team Leader approval.
- Generated files (cooked assets, compiled shaders, build output) are gitignored and live under the build tree or `assets/cooked/`.

## 2. Modules

The engine ships five static libraries plus one executable. Each is a CMake target with the same name as the directory.

| Module | Target | Kind | Owner | Purpose |
|---|---|---|---|---|
| `core` | `engine_core` | static lib | Team Leader | Foundational types, math, memory, ECS, event bus, file system, threading primitives. |
| `rendering` | `engine_rendering` | static lib | Rendering Lead | Win32 window, DX12 device, swapchain, frame graph, mesh/material/lighting, camera. |
| `networking` | `engine_networking` | static lib | Networking Lead | Winsock2 sockets, transport, packet serializer, connection manager, entity sync. |
| `tools` | `engine_tools` | static lib | Tools Lead | Logger, config, asset importer, ImGui editor stub, profiler hooks. |
| `app` | `engine` | executable | Team Leader | Wires modules together. Owns `WinMain`, the main loop, and module bootstrap order. |

### 2.1 Module dependency graph

Allowed `target_link_libraries` edges (acyclic, top-down):

```
                       ┌─────────┐
                       │   app   │
                       └────┬────┘
            ┌───────────────┼────────────────┬──────────────┐
            ▼               ▼                ▼              ▼
      ┌─────────┐    ┌────────────┐   ┌────────────┐   ┌────────┐
      │rendering│    │ networking │   │   tools    │   │  core  │
      └────┬────┘    └─────┬──────┘   └─────┬──────┘   └────────┘
           │               │                │
           └───────────────┴────────────────┘
                           │
                           ▼
                       ┌────────┐
                       │  core  │
                       └────────┘
```

- `core` depends on **nothing** in this engine. It may link to standard library, DirectXMath, and Windows SDK only.
- `rendering`, `networking`, and `tools` may link to `core`.
- `rendering`, `networking`, `tools` MUST NOT link to each other. Any cross-cutting need is solved by lifting the shared concept into `core` or by explicit composition in `app`.
- `app` links to all four.
- Tests link to the module under test plus `core` plus GTest. No test target links to another test target.

Violations of this graph fail CI (a CMake check enumerates linked targets).

## 3. Module Internals

Each module follows the same internal layout:

```
src/<module>/
├── CMakeLists.txt
├── public/<module>/        # Headers visible to other modules and to app
│   └── ...                 # Include as: #include <core/math/Vec3.h>
├── internal/               # Implementation-private headers (not exported)
└── *.cpp                   # Implementation
```

CMake configures `public/` as a `PUBLIC` include directory and `internal/` as `PRIVATE`. Other modules cannot include from `internal/`; the build will refuse.

### 3.1 Core module contents

```
src/core/public/core/
├── math/         (Vec2/3/4, Mat3/4, Quat, Transform, AABB, Frustum)         — task #17
├── memory/       (ArenaAllocator, PoolAllocator, ScopedArena, Handle<T>)     — task #18
├── ecs/          (Entity, ComponentRegistry, World, View, System)            — task #19
├── events/       (EventBus, InputState, RawInput dispatch)                   — task #20
├── fs/           (Path, FileHandle, MemoryMappedFile, AssetLocator)          — task #21
├── threading/    (SpinLock, SRWLockGuard, JobHandle stub)
├── time/         (Clock, FrameTimer, FixedTimestep)
└── containers/   (FixedVector, RingBuffer, SmallVector, BitSet)
```

### 3.2 Rendering module contents (high level)

Owned by Rendering Lead; final shape per `docs/scope-rendering.md`.

```
src/rendering/public/rendering/
├── Window.h, WindowEvent.h         — Win32 window + WM_INPUT translation to event bus
├── GpuDevice.h, Swapchain.h        — DX12 context
├── FrameGraph.h, RenderPass.h      — declarative pass graph
├── Mesh.h, Material.h, Shader.h    — runtime resources
├── Camera.h, Light.h               — scene-side data
└── DebugDraw.h
```

### 3.3 Networking module contents (high level)

Owned by Networking Lead; final shape per `docs/scope-networking.md`.

```
src/networking/public/networking/
├── Socket.h, Endpoint.h            — Winsock2 wrapper
├── Packet.h, Serializer.h          — wire format
├── Transport.h                     — reliability layer over UDP
├── Connection.h, Session.h         — connection manager and session loop
└── Snapshot.h, LagComp.h           — entity sync and lag compensation
```

### 3.4 Tools module contents (high level)

Owned by Tools Lead; final shape per `docs/scope-tools.md`.

```
src/tools/public/tools/
├── Logger.h
├── Config.h
├── AssetImporter.h, AssetWriter.h
├── Profiler.h
└── editor/                         — ImGui-based editor stubs
```

### 3.5 App module contents

```
src/app/
├── WinMain.cpp                     — entry point
├── Engine.h, Engine.cpp            — top-level lifecycle
└── BootstrapOrder.cpp              — fixed module init/shutdown sequence
```

## 4. Public API Boundaries

The public surface of a module is exactly the set of headers under `src/<module>/public/<module>/`. Anything else is implementation detail.

Hard rules:

1. **No `using namespace` in public headers.** Period.
2. **No DX12/Win32/Winsock types in public headers** of other modules. `rendering` may expose `void* GetNativeDevice()` style escape hatches, but `core`/`networking`/`tools` public headers are platform-clean.
3. **No template-heavy implementation in public headers** unless the type is small (`Vec3`, `Handle<T>`). Larger templates go behind explicit instantiations.
4. **PCH is per-module**, not project-wide. Each module's PCH covers its own implementation files, never public headers.
5. **No global state** owned by a module. Lifetimes are tied to instances created and destroyed by `app`. Logger is the lone exception (process-global, tied to `app` lifetime).
6. **No cyclic includes between modules.** Static analysis in CI walks `#include` graphs and fails on cycles.
7. **Header naming:** `PascalCase.h` for types, `lowercase.h` for "module-of-frees" (e.g., `core/math/intersect.h`). Match the dominant export.
8. **Forward-declare aggressively** in public headers; pull full definitions in `.cpp`.

## 5. Bootstrap and Shutdown Order

Owned by `app`. Fixed in `BootstrapOrder.cpp`:

**Init:**
1. `core::Logger` (the only thing that can be used during failures of later steps)
2. `core::Config` load
3. `core::FileSystem`
4. `core::EventBus`
5. `rendering::Window` (Win32 message pump online)
6. `rendering::GpuDevice` + `Swapchain`
7. `core::EcsWorld`
8. `networking::Session` (if multiplayer enabled)
9. `tools::Editor` (DevRel builds only)
10. Game-side scene load

**Shutdown** runs in reverse, with one explicit GPU flush between rendering shutdown and module teardown.

## 6. CMake Conventions

- One `CMakeLists.txt` per module under `src/<module>/`, owned by that module's lead.
- The top-level `CMakeLists.txt` is owned by Team Leader and Tools Lead jointly.
- Targets are namespaced: `engine::core`, `engine::rendering`, etc., via `add_library(... ALIAS ...)`.
- Public headers are exposed via `target_include_directories(... PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/public)`.
- Compile features: `target_compile_features(<target> PUBLIC cxx_std_20)`.

## 7. What Belongs Where: Tie-Breakers

When two modules could plausibly own a thing, use these rules:

- **If it's a value type with no platform dependency → core.** (e.g., `AABB`, `Color`, `Quat`.)
- **If it touches the GPU → rendering.** Even if it sounds like a "tool" (e.g., screenshot capture).
- **If it touches a socket → networking.**
- **If it's offline-only or build-time → tools.** (e.g., asset importer.)
- **If it's a config file format → tools.** Reading config at runtime is `core/config` calling into `tools::Config` parser through a registered loader; the parser does not run in the shipping game without `tools` linked in. (In v1 `tools` is always linked, so this is a future-proofing rule.)
- **If it's logging → tools::Logger, even though core uses it.** Core uses Logger via a thin forward-declared interface defined in `core/log.h`; Tools provides the implementation and registers it at startup.

## 8. Out of Scope for v1

- Plugin system / dynamic loading of modules.
- Splitting `tools` into separate `editor` and `pipeline` modules. (Will revisit when the editor grows beyond a stub.)
- A scripting module.
