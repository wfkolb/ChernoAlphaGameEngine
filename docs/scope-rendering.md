# Scope: Rendering Lead

Status: Approved (Phase 1)
Owner: Team Leader (this doc); Rendering Lead (the work it scopes).
References: `architecture.md`, `module-structure.md`, `ecs-design.md`, `coding-standards.md`.

This is the binding scope for the Rendering Lead through Phases 2–4. Anything outside this scope is owned by another lead or by the Team Leader; do not implement it without an explicit handoff.

---

## What you own

The `engine_rendering` static library and everything under `engine/src/rendering/` and `engine/shaders/`.

In particular:

1. **Win32 window creation and lifetime.** HWND, message pump, window-resize signaling.
2. **DX12 device, command queues, command lists, fences.** Including the debug layer in Debug/DevRel.
3. **Swapchain** (DXGI 1.6) — including HDR probing (probe only; HDR rendering is out of scope for v1).
4. **Frame graph** — declarative pass description, transient-resource lifetime, automatic resource state transitions.
5. **Pipeline state objects** — caching, hot-reload in DevRel builds.
6. **Mesh and material runtime** — vertex/index buffers, descriptor binding, per-frame upload heap, bindless textures via descriptor heap.
7. **HLSL shader pipeline** — DXC-based compile-time and DevRel runtime compilation, root signature derivation, reflection.
8. **PBR lighting** — direct lighting (Cook-Torrance GGX), simple image-based lighting (prefiltered cubemap + BRDF LUT). Shadow maps (cascaded for directional, single map for spot/point) are in scope.
9. **Camera system** — perspective and orthographic projection, frustum extraction, basic FPS controller component.
10. **Debug rendering** — line/sphere/box/text gizmos, callable from any module via the public `DebugDraw.h` API.
11. **Phase 4 integration:** validating the final frame graph end-to-end and confirming debug overlays work after wiring the engine together (task #40).

## What you do NOT own

- **ECS, math library, allocators, file I/O, event bus.** All of these live in `core` and are owned by the Team Leader. Use them, do not duplicate them.
- **Window event distribution beyond translation.** Your window translates `WM_*` messages into `core::events::WindowEvent` and `core::events::RawInput`, then publishes them on the event bus. Routing/handling is done by other systems. Window owns only the HWND lifetime and the swapchain-resize hook.
- **Asset import (glTF, PNG, HDR, cubemap baking).** Owned by Tools (asset importer). You consume the runtime `.easset` format, which is documented in `scope-tools.md`.
- **Logger, config, profiler implementation.** Use `LOG_*` macros, `tools::Config`, and `tools::Profiler::Scope` markers. Don't reimplement.
- **Networking entity sync.** You do not push render state across the network. Networking reads ECS components after `LateUpdate`; it doesn't talk to you directly.
- **Editor UI.** ImGui rendering passes integrate into the frame graph (you provide the pass), but the editor's content is owned by Tools.
- **Job system / multi-threaded recording.** v1 is single-threaded for command recording. Do not introduce a worker pool.
- **Ray tracing as a primary path, DLSS/FSR/XeSS, mesh shaders as primary path.** All explicitly out of v1 scope per `architecture.md` §11.

## Dependencies on other modules

| You depend on | For | Owner |
|---|---|---|
| `core::math` | Vec/Mat/Quat/Transform/Frustum/AABB | Team Leader, task #17 |
| `core::memory` | Frame arena, pool allocators for pipeline states and descriptors | Team Leader, task #18 |
| `core::ecs` | Component definitions you register: `Camera`, `Light`, `Renderable`, `MeshHandle`, etc. | Team Leader, task #19 |
| `core::events` | Inbound `WindowEvent`, `RawInput` (you publish, others subscribe) | Team Leader, task #20 |
| `core::fs` | Loading `.easset` and shader bytecode files | Team Leader, task #21 |
| `tools::Logger` | All log output | Tools Lead, task #32 |
| `tools::AssetImporter` cooked format spec | Mesh and texture binary layout | Tools Lead, task #33 |
| `tools::Profiler` | PIX markers and CPU scope timing | Tools Lead |
| Build system | DXC invocation rule, shader hot-reload pipeline in DevRel | Tools Lead, task #31 |

## Phase 2 deliverables (scope/design docs)

You will produce four design documents under `engine/docs/`. These are written before any code is committed to `engine_rendering` beyond skeleton CMakeLists.

| Task | Deliverable | Required content |
|---|---|---|
| #6 | `docs/rendering-window-and-dx12.md` | Win32 class registration; HWND lifecycle; DXGI factory; adapter selection (highest-perf adapter that supports FL 12_1); device + command queue layout (graphics, compute, copy); swapchain config (3-buffer, flip-discard, tearing-allowed); debug layer activation rules; resize handling. |
| #7 | `docs/rendering-frame-graph.md` | Pass declaration API; resource transient/persistent classification; automatic barrier insertion; PSO cache keying; per-frame fence pattern; how the editor's ImGui pass integrates. |
| #8 | `docs/rendering-mesh-material-shader.md` | Vertex layouts (positions only / static mesh / skinned variants); index buffer format (uint32 default); descriptor heap layout (one CBV/SRV/UAV heap, one sampler heap); root signature shape; HLSL include paths; DXC invocation flags; runtime shader hot reload contract. |
| #9 | `docs/rendering-pbr-and-camera.md` | BRDF model; light component shape; shadow approach (CSM 4-cascade for sun, single-map for point/spot); IBL prefilter pipeline (consumes cubemap from importer); camera component, projection helpers, frustum extraction; debug-draw integration. |

Each design doc is reviewed and approved by the Team Leader before Phase 3 implementation tasks (#22–26) start.

## Phase 3 deliverables (code)

In ID order: tasks #22, #23, #24, #25, #26.

Definitions of done:

- **#22 — DX12 context and swapchain.** Engine starts, opens a window, clears the back buffer to a known color, runs at vsync, resizes cleanly. Debug layer is silent. Smoke test in `tests/rendering/` passes.
- **#23 — Mesh pipeline and vertex/index buffers.** A static mesh loaded from a cooked `.easset` is rendered with a flat shader. Vertex/index buffers come from a per-frame upload + a persistent default-heap path.
- **#24 — HLSL shader compilation and binding.** All shaders are compiled through DXC. DevRel build supports hot reload via file watcher. Shader reflection drives root-signature creation.
- **#25 — PBR material system and lighting.** A material asset rendered with one directional light + IBL produces correct results validated against a reference image (within 2% RMSE).
- **#26 — Camera controller and debug rendering.** FPS camera moves with WASD + mouse look. `DebugDraw::Line/Sphere/Box/Text` work from any module and are submitted in a dedicated frame-graph pass.

## Public API constraints

These are reminders of `module-structure.md` §4 specific to your module:

- No DX12 types in your public headers. `ID3D12Device*` etc. are private. The opaque escape hatch is `void* GpuDevice::nativeHandle()`, returning the `ID3D12Device*` for tooling integration only — never used by other engine modules.
- No `<windows.h>` in your public headers. Forward-declare `HWND` as `struct HWND__*` if needed and gate behind a `// IWYU pragma: keep` if your CI complains. Better yet, don't expose HWND at all — provide `Window::nativeWindowHandle()` returning `void*`.
- The frame graph's pass-declaration API is templated. Templates live in the public header; their bodies live in `*.inl`.
- Constants in public headers use `kPascalCase`: `kMaxBackBuffers = 3;`, `kFrustumPlaneCount = 6;`.

## Coordinate system reminders

- Right-handed, Y-up, +Z forward. `core::math::Mat4` matches this.
- Clip-space depth is `[0, 1]`, reverse-Z (near = 1.0, far = 0.0) for perspective. Document the depth convention prominently in the camera doc.
- Texture origin is top-left in HLSL; `.easset` textures store top-left-origin to match.

## Performance targets (v1 baseline)

These are aspirational targets for the test lead's benchmarks (task #38) — they don't gate Phase 3 task completion, but they shape design choices.

| Metric | Target |
|---|---|
| Empty frame (clear + present) at 1080p | ≤ 0.5 ms GPU on a mid-range desktop GPU |
| 10k static mesh draws | ≤ 4 ms CPU recording |
| Shader hot-reload latency | ≤ 250 ms from file save to next frame |
| Frame graph compile (per frame) | ≤ 0.05 ms CPU |

## Communication

- Surface design questions to the Team Leader via SendMessage; surface cross-module-API questions to both the Team Leader and the affected lead.
- Do not block on Tools' asset format spec — propose your consumed format in the mesh/material design doc and Tools will conform unless they raise concerns within review.
