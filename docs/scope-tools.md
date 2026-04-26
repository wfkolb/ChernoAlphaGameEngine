# Scope: Tools Lead

Status: Approved (Phase 1)
Owner: Team Leader (this doc); Tools Lead (the work it scopes).
References: `architecture.md`, `module-structure.md`, `ecs-design.md`, `coding-standards.md`.

This is the binding scope for the Tools Lead through Phases 2–4. Tools is unusually wide — it includes the build system, the asset pipeline, the logger, the config reader, and the editor stub.

---

## What you own

The `engine_tools` static library, the top-level `CMakeLists.txt` (jointly with the Team Leader), the `vcpkg.json` manifest, the `cmake/` helper directory, and the `assets/` directory.

In particular:

1. **CMake build system.** Top-level `CMakeLists.txt`, per-module presets, vcpkg manifest, warning configuration, shader-compile rules, asset-cooking rules, test discovery, install rules.
2. **CI configuration.** GitHub Actions (or equivalent) workflow files. CI is wired in #35 with the Test Lead, but the runner setup and matrix are yours.
3. **Logger.** Public macros (`LOG_TRACE`/`INFO`/`WARN`/`ERROR`/`FATAL`). Console + rotating file backends. VT-100 color in console, plain text in file.
4. **Configuration system.** TOML-backed config reader (`tools::Config`). Loads `engine.toml` next to the executable, then user override from `%APPDATA%/<engine>/engine.toml`. Provides typed accessors with defaults.
5. **Asset importer (offline).** Reads source assets (glTF 2.0, PNG, HDR, OBJ for fallback), produces packed `.easset` runtime files. Optimization via `meshoptimizer`, mip generation via `stb_image_resize2`, cubemap prefilter via your own DX12 compute (or CPU fallback).
6. **`.easset` runtime format.** The single binary asset format consumed by the renderer at runtime. You own the spec.
7. **Profiler integration.** PIX markers on the GPU side (rendering's `DebugDraw` and frame graph use these), CPU `Profiler::Scope` markers via `WinPixEventRuntime`. Optional Tracy integration (build-time flag, off by default).
8. **Stub ImGui scene editor.** Window, ECS entity tree, component inspector (driven by the reflection `inspect` function pointer), gizmos for translate/rotate, no save/load in v1.
9. **Phase 4 integration:** asset pipeline end-to-end validation (task #42) — confirm a glTF + PNG inputs cook to `.easset` and render correctly through `engine_rendering`.

## What you do NOT own

- **GPU resource management.** You feed bytes into the renderer; you don't allocate textures. The renderer's `Texture` and `Mesh` types own their GPU lifetimes.
- **The frame graph or any HLSL.** Owned by Rendering Lead. (Your asset importer does generate the IBL prefiltered cubemap, which is allowed to use a small fixed compute shader; that shader lives under `tools/asset_importer/shaders/` and uses DXC just like the runtime, but it is offline-only.)
- **The ECS itself.** You consume reflection metadata; you do not define `World`, `Archetype`, `Entity`.
- **Networking.** You parse the `[network]` config section but do not implement transport.
- **Test framework wiring per-module.** You set up the GTest infrastructure (#35 jointly with Test Lead), then leads add their own tests.
- **Application bootstrap order.** Owned by Team Leader in `app/BootstrapOrder.cpp`. You provide the right APIs to bootstrap.

## Dependencies on other modules

| You depend on | For | Owner |
|---|---|---|
| `core::fs` | File I/O for asset reading and writing | Team Leader, task #21 |
| `core::memory` | Arena allocators for asset import working memory | Team Leader, task #18 |
| `core::math` | Vec/Mat for asset transformations | Team Leader, task #17 |
| `core::ecs::reflection` | The `inspect` and `serialize` function pointer registry that powers the editor and config-driven entity creation | Team Leader, task #19 |
| Rendering's mesh/material public types | What the importer must produce shapes for | Rendering Lead |
| Networking's `[network]` config schema | What keys to parse | Networking Lead |

## Phase 2 deliverables (scope/design docs)

You will produce three design documents under `engine/docs/`. These are written before substantial code goes into `engine_tools`. The build-system finalization in task #31 is partially blocked on #13.

| Task | Deliverable | Required content |
|---|---|---|
| #13 | `docs/tools-build-and-asset-pipeline.md` | Top-level CMake structure; presets (`debug`, `devrel`, `release`); vcpkg manifest contents; warning policy file; shader compile rule (DXC inputs/outputs, dependency tracking); asset cooking rule (`.gltf` → `.easset` with proper rebuild dependencies); install layout; CI matrix (Win10 + Win11, Debug + Release). |
| #14 | `docs/tools-logging-and-config.md` | Logger architecture (frontend macros → backend sinks → file rotation strategy); thread-safety of logging; log-level filtering at compile time vs. runtime; config schema with the canonical sections (`[engine]`, `[render]`, `[network]`, `[editor]`, `[log]`); typed accessor API; user-override loading order. |
| #15 | `docs/tools-editor-and-profiler.md` | Editor window layout; ECS entity tree and component inspector; gizmo behavior; integration with the renderer's frame graph (one ImGui pass at the end of the render phase); profiler scope macros and PIX markers; Tracy build-time toggle. |

Each design doc is reviewed and approved by the Team Leader before implementation tasks (#31–34) start. Note that #31 (build system finalization) is critical-path for everyone — get the design done early.

## Phase 3 deliverables (code)

In ID order: tasks #31, #32, #33, #34.

Definitions of done:

- **#31 — CMake finalized.** All five module targets (`engine_core`, `engine_rendering`, `engine_networking`, `engine_tools`, `engine`) build clean from a fresh checkout with `cmake --preset debug && cmake --build --preset debug`. CI runs the same. Shader compile and asset cooking are wired with proper dependency tracking — touching a `.hlsl` file rebuilds only that pass; touching a `.gltf` recooks only that asset.
- **#32 — Logger.** Macros emit to console (with color in a TTY) and to `%LOCALAPPDATA%/<engine>/logs/engine-YYYYMMDD-HHMMSS.log`. File rotates at 10 MB or per session, whichever comes first. Format string is `std::format`-compatible. Multi-threaded log calls are serialized with a per-sink mutex; trace-level calls compile to nothing in Release.
- **#33 — Asset importer.** A glTF 2.0 file with embedded textures cooks to a single `.easset` package. PBR material parameters round-trip. Mips are generated. The runtime loads the file zero-copy where possible (via `MemoryMappedFile` from `core/fs`).
- **#34 — Config reader and editor stub.** `engine.toml` loads at startup; missing keys use documented defaults. Editor window opens (DevRel only), shows entity list, allows selecting an entity and editing its `Transform` via a translate gizmo. No undo, no save in v1.

## Public API constraints

- Logger frontend headers must be includable from `core` (the only intentional reverse-direction dependency in the engine, mediated by a forward-declared interface in `core/log.h`). The `LOG_*` macros expand to calls into a stable C-ABI-compatible function pointer registered by `tools::Logger` at startup.
- Config types are plain values; no `tools::Config` object handed out as a long-lived pointer. Subsystems read what they need at startup and cache it.
- Asset importer is **offline-only** — it has its own executable target (`asset_cooker`) built from `engine_tools`. The runtime engine does not link the importer.
- No third-party library types in your public headers. `tinygltf::Model` is private; you expose your own intermediate-representation types if needed (and probably don't need to — you produce `.easset` bytes).

## `.easset` format conventions

- Little-endian, 64-bit aligned, magic `"EASS"`.
- 16-byte header: magic, version, asset-type, total size.
- Followed by a TOC of named sections (mesh, material, textures, mips).
- Versioned: bump the version when the layout changes; the runtime refuses to load an unknown version.
- Document the spec inline in `docs/tools-build-and-asset-pipeline.md` and keep an authoritative C++ struct definition in `tools/AssetWriter.h` shared with the runtime loader.

## CMake conventions you enforce

- Targets aliased as `engine::<module>`.
- `cxx_std_20`, `/W4 /WX`, `/permissive-`, `/Zc:__cplusplus`, `/Zc:preprocessor`.
- LTO on Release.
- PCH per module.
- `add_test` calls for every `*_tests` target via `gtest_discover_tests`.
- Configure-time check that no module links to a peer module other than the allowed edges in `module-structure.md` §2.1.

## Performance targets (v1 baseline)

| Metric | Target |
|---|---|
| Cold full-engine build (8-core desktop, Ninja, Debug) | ≤ 90 s |
| Warm rebuild after changing one `.cpp` in `core` | ≤ 5 s |
| Cooking a 5k-triangle glTF + 4 textures | ≤ 1 s |
| Logger steady-state throughput, single thread, info-level | ≥ 250k msgs/s |
| Editor frame overhead in DevRel | ≤ 0.5 ms CPU |

These are aspirational baselines for the test lead's benchmarks (task #38).

## Communication

- You unblock everyone via #31. Get the build green and minimally functional early; iterate on niceties later.
- Coordinate with Test Lead on the CI workflow file; you own the runner config, they own the matrix of test invocations.
- Coordinate with Rendering Lead on the `.easset` mesh/material schema before implementing the importer.
