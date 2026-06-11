# Phase 10 — Deficit Resolution Plan

**Status:** Planning
**Source:** Gap analysis against original architecture docs (`docs/architecture.md`, `docs/scope-*.md`, `docs/rendering-*.md`, `docs/networking-*.md`, `docs/ecs-design.md`, `docs/tools-editor-and-profiler.md`) plus editor usability review (2026-06-07).
**Total tasks:** 25 actionable items + 1 informational note.

---

## 1. Dependency Graph

```
Wave 1 (no deps — maximum parallelism)
│
│  A2  A3  E1  E2  E4  E5  E6  N1  N2  N3  R4  R5  R6  R7  T1
│
Wave 2 (unblocked after Wave 1 completions)
│
│  R1 ──── needs A3
│  R3 ──── needs R4
│  E3 ──── needs A3 + R4
│  T4 ──── needs T1
│  N4 ──── needs T1
│  N5 ──── needs T1
│
Wave 3 (unblocked after Wave 2 completions)
│
│  R2 ──── needs R1
│  T2 ──── needs R1 + R3
│  T3 ──── needs R1
│
Wave 4 (cleanup — after Wave 3)
│
│  A1 ──── needs R1 + R2 + R3
```

**Critical path to a visually complete renderer:** `R4 → R3 → R1 → R2 → A1`

**Critical path to a usable editor:** `A3 → E3` (material selection); `E5` (PIE cursor capture) is independent and high-value alone.

---

## 2. Wave Summary

### Wave 1 — No Dependencies (15 tasks, all parallel)

| ID | Title | Owner | Est. |
|----|-------|-------|------|
| A2 | `core::time::Clock` | Core Lead | S |
| A3 | Consolidate `Renderable` component | Rendering Lead | M |
| E1 | Replace gizmo stub with ImGuizmo | Tools Lead (editor) | M |
| E2 | Profiler — `PROFILE_SCOPE` + PIX markers | Tools Lead | M |
| E4 | `ColliderComponent::enabled` flag + mesh shape overlays | Physics Lead + Tools Lead | S |
| E5 | PIE cursor capture | Tools Lead (editor) | S |
| E6 | Embed asset preview inside AssetBrowserPanel | Tools Lead (editor) | S |
| N1 | XInput 1.4 gamepad | Networking Lead | M |
| N2 | Update networking design docs to 64 Hz | Team Leader | XS |
| N3 | XOR-obfuscation security hook | Networking Lead | S |
| R4 | Texture cooking pipeline (PNG/HDR → `.easset`) | Tools Lead | L |
| R5 | Debug line shaders + DebugDraw GPU pass | Rendering Lead | M |
| R6 | `FpsCameraController` ECS component + system | Rendering Lead | S |
| R7 | Skinned mesh pipeline stub | Rendering Lead | M |
| T1 | CI pipeline (GitHub Actions) | Tools Lead | M |

### Wave 2 — Unblocked After Wave 1 (6 tasks)

| ID | Title | Owner | Depends On | Est. |
|----|-------|-------|------------|------|
| E3 | Material assignment widget in Inspector | Tools Lead (editor) | A3 + R4 | M |
| N4 | Client prediction/reconciliation integration test | Networking Lead + Test Lead | T1 | M |
| N5 | Packet serializer fuzz harness | Networking Lead | T1 | S |
| R1 | Dynamic light buffer | Rendering Lead | A3 | L |
| R3 | IBL pipeline (prefilter cubemap + BRDF LUT) | Rendering Lead + Tools Lead | R4 | L |
| T4 | clang-format + clang-tidy CI gates | Tools Lead | T1 | S |

### Wave 3 — Unblocked After Wave 2 (3 tasks)

| ID | Title | Owner | Depends On | Est. |
|----|-------|-------|------------|------|
| R2 | Shadow maps (CSM + spot/point) | Rendering Lead | R1 | XL |
| T2 | Rendering golden image tests | Test Lead + Rendering Lead | R1 + R3 | M |
| T3 | Rendering benchmark (`rendering_bench`) | Test Lead + Rendering Lead | R1 | S |

### Wave 4 — Cleanup (1 task)

| ID | Title | Owner | Depends On | Est. |
|----|-------|-------|------------|------|
| A1 | Remove `FlatShadePass` public API | Rendering Lead | R1 + R2 + R3 | S |

**Size key:** XS < 1 day · S = 1–2 days · M = 3–5 days · L = 1–2 weeks · XL = 2–3 weeks

---

## 3. Task Specifications

---

### A1 — Remove `FlatShadePass` Public API *(Wave 4)*

**Owner:** Rendering Lead
**Depends on:** R1, R2, R3

`src/rendering/public/rendering/FlatShadePass.h` was a Phase 3 stepping stone for `SpinDemo`. It survived as a public API. Once the full PBR pipeline (R1 + R2 + R3) is in place, `SpinDemo` must be ported to use the standard opaque pass and `FlatShadePass` removed entirely.

**Acceptance criteria:**
- `FlatShadePass.h` deleted; no references remain in any public header.
- `SpinDemo` renders correctly using the PBR opaque pass.
- Build clean with `/W4 /WX`.

---

### A2 — `core::time::Clock` *(Wave 1)*

**Owner:** Core Lead
**Depends on:** —

`scope-networking.md` forbids direct `QueryPerformanceCounter` calls; `core::time::Clock` must be the single abstraction. Currently absent from `core/public/core/`.

**Deliverables:**
- `src/core/public/core/time/Clock.h` — `Clock::now() → uint64_t` (nanoseconds), `Clock::ticksPerSecond()`, `Clock::toSeconds(uint64_t ticks) → double`.
- Replace all `QueryPerformanceCounter` / `QueryPerformanceFrequency` call sites in `networking/` and `physics/` with `Clock::now()`.
- `tests/core/time/ClockTests.cpp` (label: unit) — monotonicity, resolution ≥ 1 µs, wrap-safety over simulated overflow.

---

### A3 — Consolidate `Renderable` Component *(Wave 1)*

**Owner:** Rendering Lead
**Depends on:** —

The design specified `rendering::Renderable` (mesh + material + castShadow + receiveShadow). What was built separates `core::MeshHandle` (id=12) from any material reference. `MeshRenderSystem` and the editor both lack a material pairing. This blocks E3 and R1.

**Deliverables:**
- Add `rendering::MaterialHandle materialHandle` and `bool castShadow`, `bool receiveShadow` to the `core::MeshHandle` component (rename to `core::RenderableComponent` or add fields in-place — Team Leader decides).
- `MeshRenderSystem::tick()` reads the material handle and passes it to the draw call.
- `ComponentEditorRegistry`: update the `MeshHandle` widget to show the material handle index (read-only until E3 is done).
- All existing tests pass; no regressions in `SpinDemo` / `ClearDemo`.

---

### E1 — Replace Gizmo Stub with ImGuizmo *(Wave 1)*

**Owner:** Tools Lead (editor)
**Depends on:** —

`ViewportPanel::drawGizmo` is a custom `ImDrawList` widget: no per-axis handles, rotate is Y-only, scale is uniform-only, `worldPerPixel = 0.01f` is hardcoded. `ImGuizmo` is already an approved dependency and in `vcpkg.json`; it is simply not called.

**Deliverables:**
- Remove `drawGizmo` custom implementation.
- Replace with `ImGuizmo::Manipulate(view, proj, op, mode, matrix)` using the current `GizmoOp` → `ImGuizmo::OPERATION` mapping (W=TRANSLATE, E=ROTATE, R=SCALE, Q=none).
- Matrix round-trip: decompose `ImGuizmo` output back to `Transform::position`, `Transform::rotation`, `Transform::scale`. Rotation must go through `Quat` not raw Euler to avoid gimbal lock accumulation.
- Snapping: pass `snapTranslate_` / `snapScale_` to `ImGuizmo::Manipulate`'s snap parameter when `Ctrl` is held.
- `TransformCommand` still pushed on mouse release (undo remains intact).
- Update collider handle comment (`TODO Phase 9`) to `TODO Phase 10` once `ImGuizmo::DrawCubes` is wired for collider handles.

---

### E2 — Profiler: `PROFILE_SCOPE` + PIX Markers *(Wave 1)*

**Owner:** Tools Lead
**Depends on:** —

`tools-editor-and-profiler.md §6` specifies CPU `ProfilerScope` RAII + `PROFILE_GPU_SCOPE` / `PROFILE_GPU_END` macros wrapping `WinPixEventRuntime`. Neither `Profiler.h` nor any `PIXBeginEvent` call exists anywhere in source.

**Deliverables:**
- `src/tools/public/tools/Profiler.h`:
  - `PROFILE_SCOPE(name)` — expands to `::engine::tools::ProfilerScope __profN{name}` in DevRel/Debug; `((void)0)` in Release.
  - `PROFILE_GPU_SCOPE(cmdList, name)` / `PROFILE_GPU_END(cmdList)` — `PIXBeginEvent` / `PIXEndEvent` in DevRel; no-op otherwise.
- `src/tools/internal/PixWrapper.h` — isolates `#include <pix3.h>` from public headers.
- Add `PROFILE_SCOPE` to the hot paths: `PhysicsWorld::step()`, `MeshRenderSystem::tick()`, `GameLoop::serverTick()`, `GameLoop::clientTick()`, `ReplicationSystem` snapshot encode/decode.
- Add `PROFILE_GPU_SCOPE` to each frame graph pass execute lambda.
- Verify in PIX: a capture shows labelled CPU + GPU ranges.

---

### E3 — Material Assignment Widget in Inspector *(Wave 2)*

**Owner:** Tools Lead (editor)
**Depends on:** A3, R4

**Deliverables:**
- Inspector widget for the `RenderableComponent` (post-A3 name) that shows:
  - Current material index (numeric) with a dropdown/combo listing all loaded materials by name.
  - Albedo factor (color picker), metallic/roughness sliders as fallback when no textures are loaded.
  - `castShadow` and `receiveShadow` checkboxes.
- `ComponentEditorRegistry`: register the widget under `RenderableComponent`.
- Material name registry: `MaterialManager` must expose `const char* getName(MaterialHandle)` for the combo. Add this accessor.
- Changing the material via the widget pushes a `MaterialChangeCommand` onto the undo stack.

---

### E4 — `ColliderComponent::enabled` + Mesh Shape Overlays *(Wave 1)*

**Owner:** Physics Lead + Tools Lead (editor)
**Depends on:** —

Two independent sub-tasks that ship together:

**Sub-task 1 — `enabled` flag:**
- Add `bool enabled = true` to `ColliderComponent` (reclaim one of the three `_pad` bytes — check static_assert after).
- `PhysicsWorld` skips disabled colliders in broad/narrow phase.
- `ColliderWidget` shows an `ImGui::Checkbox("Enabled", &col.enabled)` at the top of the widget.

**Sub-task 2 — ConvexHull / TriangleMesh overlays:**
- `ViewportPanel::drawOverlays` currently has no case for `ColliderComponent::Shape::ConvexHull` or `TriangleMesh`.
- For `ConvexHull`: draw the hull vertex edges loaded from the `.easset` `COLL` section (fetch via `loadEasset`; cache by asset path per-frame).
- For `TriangleMesh`: draw a wireframe of the triangle soup (same source). Cap at 2000 edges with a `[truncated]` label if the mesh is dense.
- Both rendered in the same green/yellow colour scheme as `Box`/`Sphere`.

---

### E5 — PIE Cursor Capture *(Wave 1)*

**Owner:** Tools Lead (editor)
**Depends on:** —

`PIEController` sets `captureMouse_ = true` on `start()` and `false` on `stop()`. `EditorApp.cpp` never reads `isCapturingMouse()`. No `SetCapture`, `ClipCursor`, or `ShowCursor` call exists. `WM_INPUT` routing already works; only the cursor side is missing.

**Deliverables:**
- In `EditorApp::onPIEStart()` (or wherever PIE start is handled):
  ```cpp
  if (pie_.isCapturingMouse()) {
      HWND hwnd = static_cast<HWND>(window_->nativeHandle());
      ShowCursor(FALSE);
      RECT r; GetClientRect(hwnd, &r);
      MapWindowPoints(hwnd, nullptr, (POINT*)&r, 2);
      ClipCursor(&r);
      SetCapture(hwnd);
      ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
  }
  ```
- In `EditorApp::onPIEStop()`: release capture, restore cursor, remove `ImGuiConfigFlags_NoMouse`.
- Escape key (or configurable bind) calls `pie_.stop()` so the user can exit at any time.
- `EditorPrefs` should expose a `pieMouseCapture` bool so users can opt out (default true).

---

### E6 — Embed Asset Preview Inside AssetBrowserPanel *(Wave 1)*

**Owner:** Tools Lead (editor)
**Depends on:** —

`MeshPreviewPanel::draw()` opens a standalone `ImGui::Begin("Asset Preview")`. Single-clicking an `.easset` fires a callback that loads into this floating panel. It should instead render inline in the right third of the browser.

**Deliverables:**
- Split `AssetBrowserPanel::draw()` into a left file-list column (`ImGui::BeginChild("##list", ...)`) and a right preview column (`ImGui::BeginChild("##preview", ...)`).
- The preview column hosts an `ImGui::Image` showing the `MeshPreviewPanel`'s SRV output directly (no separate window).
- `MeshPreviewPanel::draw()` refactored to `drawInline(ImVec2 size)` — no `ImGui::Begin`/`End`; caller provides the child region.
- The existing floating `"Asset Preview"` window is removed; `EditorApp` no longer opens it.
- Orbit interaction (mouse drag to rotate the preview) works when the cursor is over the preview column.

---

### N1 — XInput 1.4 Gamepad *(Wave 1)*

**Owner:** Networking Lead (input subsystem)
**Depends on:** —

`architecture.md §3` specifies XInput 1.4. `InputSystem` handles only `WM_INPUT` (keyboard + mouse).

**Deliverables:**
- `src/core/input/InputSystem.cpp`: call `XInputGetState` each `InputSystem::update()` for up to 4 controllers.
- Map left stick → `lookYawDelta` / `lookPitchDelta` in `InputFrame`; right stick → `moveX` / `moveZ`; A button → `jump`; right trigger → `fire`.
- Dead-zone: 7849 for sticks (XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE), 30 for triggers.
- `InputBinding` extended with a `GamepadAxis` / `GamepadButton` source type so bindings in `input.toml` can reference gamepad inputs.
- `tests/core/input/XInputTests.cpp` (label: unit) — mock `XInputGetState` via a stub function pointer injected at test time; verify dead-zone, axis mapping, button mapping.

---

### N2 — Update Networking Design Docs to 64 Hz *(Wave 1)*

**Owner:** Team Leader
**Depends on:** —

`networking-lag-compensation.md`, `networking-architecture.md`, and `scope-networking.md` all state 30 Hz simulation tick. Implementation runs at 64 Hz. `kRewindWindowTicks = 6` was calibrated for 30 Hz (= 200 ms); at 64 Hz it equals ~94 ms — below the 200 ms design target.

**Deliverables:**
- Update all "30 Hz" references in networking docs to "64 Hz".
- Recalculate `kRewindWindowTicks`: for 200 ms at 64 Hz → `kRewindWindowTicks = 13`. Update the constant and the doc.
- Update `kDefaultTickRate` constant comment wherever it appears.
- No code changes beyond the constant update.

---

### N3 — XOR-Obfuscation Security Hook *(Wave 1)*

**Owner:** Networking Lead
**Depends on:** —

`scope-networking.md` requires a pre-shared-key XOR hook as a documented future replacement surface. Must be clearly labelled as not real security.

**Deliverables:**
- `src/networking/public/networking/PacketObfuscation.h`:
  ```cpp
  // NOT cryptographic security. XOR obfuscation only — a future DTLS layer
  // should replace this entirely. See scope-networking.md §security posture.
  void xorObfuscate(std::span<uint8_t> packet, std::span<const uint8_t> key);
  ```
- Hook called in `Session` send/receive path when a non-empty key is configured via `[network].obfuscationKey` in `engine.toml`. Empty key = no-op (default).
- `tests/networking/ObfuscationTests.cpp` (label: unit) — round-trip encode/decode, empty-key no-op, partial-packet alignment.

---

### N4 — Client Prediction / Reconciliation Integration Test *(Wave 2)*

**Owner:** Networking Lead + Test Lead
**Depends on:** T1

The `PredictionBuffer` and `SnapshotBuffer` exist. The full `ReconcileMessage` → rollback → re-simulate path has no end-to-end test.

**Deliverables:**
- `tests/networking/PredictionIntegrationTests.cpp` (label: integration):
  - In-process server + client via `Session::createLocalPair`.
  - Server simulates 10 ticks of a moving entity; client predicts in parallel.
  - Inject a 2-tick position divergence on the server; verify client detects divergence ≥ `kReconcileThreshold` and re-simulates.
  - Verify final client position matches authoritative server position within 1 mm.
- Label `integration` so CI gates it on GPU/physical runner only.

---

### N5 — Packet Serializer Fuzz Harness *(Wave 2)*

**Owner:** Networking Lead
**Depends on:** T1

`scope-networking.md` and task #28 require a fuzz harness: random byte input → either valid parse or clean rejection, never UB.

**Deliverables:**
- `tests/networking/SerializerFuzzTests.cpp` (label: unit):
  - Seeds `std::mt19937` from a known constant; logs seed at test start; `--gtest_filter` accepts `--seed=N` override.
  - Generates 10 000 random byte buffers of lengths 0–1400; feeds each into `BitReader`; asserts no crash, no read past end, no UB (ASAN/UBSAN should be on in CI Debug).
  - Generates 1 000 partially-valid packets (valid header, corrupted payload); verifies graceful rejection.
- CI Debug build must have `/fsanitize=address` or equivalent enabled (add to `cmake/Warnings.cmake` for Debug config only).

---

### R1 — Dynamic Light Buffer *(Wave 2)*

**Owner:** Rendering Lead
**Depends on:** A3

`OpaquePS.hlsl:83` has a hardcoded `float3 L = normalize(0.5, 1.0, 0.3)` with the comment "placeholder; real lights go in a light buffer." Up to 64 lights per frame were specified in `rendering-pbr-and-camera.md §2.2`.

**Deliverables:**
- `GpuLight` struct (64 bytes, as specified in `rendering-pbr-and-camera.md §2.2`) in a shared header includable from HLSL via `CommonTypes.hlsli`.
- Per-frame upload-heap structured buffer (`kMaxLights = 64`) written by a new `LightCullSystem` that iterates `View<Transform, rendering::Light>` and builds the `GpuLight` array.
- `OpaquePS.hlsl`: replace hardcoded light with a loop over the `gLights` structured buffer; light count passed via the per-frame CB.
- `shaders/common/CommonTypes.hlsli` updated with `GpuLight` definition and `PerFrameConstants.lightCount`.
- `tests/rendering/LightBufferTests.cpp` (label: unit) — `LightCullSystem` builds the correct `GpuLight` array from a known set of ECS entities; verify position, type, intensity encoding.

---

### R2 — Shadow Maps (CSM + Spot/Point) *(Wave 3)*

**Owner:** Rendering Lead
**Depends on:** R1

`rendering-pbr-and-camera.md §3` specifies: CSM 4-cascade (`kShadowCascadeCount = 4`, 2048×2048 per cascade) for directional lights; single 512×512 map per shadow-casting spot; cube map (6×512×512) per shadow-casting point. Up to 8 shadow-casting non-directional lights (`kMaxShadowCastingSpots = 8`).

**Deliverables:**
- `shaders/shadow/ShadowVS.hlsl` + `ShadowPS.hlsl` — depth-only pass.
- Shadow map render targets and DSVs allocated in `GpuDevice`.
- Frame graph: shadow passes execute before the opaque pass; produce `D32_FLOAT` texture arrays.
- Cascade split using the practical-split formula (`splitLambda = 0.95`, configurable via `[render].shadowSplitLambda`).
- Stable shadow mapping: snap cascade ortho frustum to texel boundaries.
- `OpaquePS.hlsl`: 4-tap PCF sampling with `ShadowPCF` comparison sampler (slot 5 in sampler heap, as per `rendering-mesh-material-shader.md §3.2`).
- Shadow maps use standard [0,1] depth (not reverse-Z) per the design doc note in `rendering-pbr-and-camera.md §3.1`.
- `Light::castShadow` must be `true` to allocate a shadow map; lights without shadows skip the shadow pass.
- Performance target: ≤ 2 ms additional GPU time at 1080p on a mid-range desktop for 1 directional + 2 spot shadow-casting lights.

---

### R3 — IBL Pipeline (Prefilter Cubemap + BRDF LUT) *(Wave 2)*

**Owner:** Rendering Lead + Tools Lead
**Depends on:** R4

`rendering-pbr-and-camera.md §1.2` specifies a prefiltered environment map (7 mip levels, `R16G16B16A16_FLOAT`) and BRDF LUT (256×256, `R16G16_UNORM`).

**Deliverables (Tools side):**
- `asset_cooker` gains a `--cubemap` mode: takes an equirectangular HDR, produces an `.easset` with a `CMAP` section (prefiltered cubemap mips) and a `BRDF` section (LUT). Offline DX12 compute or CPU fallback.
- `loadEasset()` extended to return optional `CpuCubemap` with mip data.

**Deliverables (Rendering side):**
- GPU upload of cubemap and BRDF LUT at scene load; SRV slots assigned in `mainHeap_`.
- `OpaquePS.hlsl`: add IBL contribution (split-sum: `diffuseIBL + specularIBL`) replacing the flat `0.03` ambient constant.
- `SceneGlobals` extended with `std::string skyboxAssetPath`.
- `Scene::activate()` loads skybox `.easset` and uploads IBL resources.
- `tests/rendering/IblTests.cpp` (label: unit) — verify LUT at (NdotV=1, roughness=0) produces expected scale/bias; verify prefiltered cubemap has 7 mip levels.

---

### R4 — Texture Cooking Pipeline *(Wave 1)*

**Owner:** Tools Lead
**Depends on:** —

`scope-tools.md` requires PNG/HDR → `.easset` with mip generation via `stb_image_resize2`. `GpuMaterial` has four texture index slots; nothing fills them. `stb_image` is in the approved deps but unused.

**Deliverables:**
- Add `stb_image` and `stb_image_resize2` to `vcpkg.json` (single-header, vendor under `third_party/stb/` per `architecture.md §5` convention).
- `importGltf()` extracts embedded textures (albedo, normal, metallic/roughness, emissive); generates full mip chain via `stb_image_resize2`; writes a `TEX` section per texture into the `.easset`.
- `.easset` format version bumped to v3; v1/v2 loaders remain forward-compatible (no `TEX` section = no textures, not an error).
- `loadEasset()` extended: `CpuMesh::textures` — `vector<CpuTexture>` where `CpuTexture` holds mip data, width, height, `DXGI_FORMAT`.
- `GpuDevice::uploadTexture(CpuTexture&) → uint32_t srvIndex` — uploads to `mainHeap_` and returns the bindless SRV index.
- `Application` mesh-load path: after `MeshManager::upload()`, upload textures and populate `GpuMaterial`'s texture index fields.
- `tests/tools/TextureCookingTests.cpp` (label: unit) — verify mip count = `floor(log2(max(w,h))) + 1`; verify round-trip for a known 4×4 PNG; verify v1 `.easset` loads without error.

---

### R5 — Debug Line Shaders + DebugDraw GPU Pass *(Wave 1)*

**Owner:** Rendering Lead
**Depends on:** —

`rendering-pbr-and-camera.md §5` specifies a `DebugDraw` GPU pass with line geometry and world-space text via a bitmap font. `DebugDraw.h` declares the API but no shaders exist (`shaders/debug/` is absent) and no frame-graph pass is registered.

**Deliverables:**
- `shaders/debug/DebugLineVS.hlsl` + `DebugLinePS.hlsl` — unlit line-list; `DebugVertex { float xyz[3]; uint32_t packedColor; }`.
- Font atlas: 8×8 pixel glyph bitmap for printable ASCII (96 glyphs) stored as a `TEX` section in a vendored `debug_font.easset`; uploaded to a fixed SRV slot.
- `DebugDraw::flush()` (called in `PostRender` phase): uploads accumulated line segments + text billboards to a per-frame upload buffer; submits a single draw call.
- `kMaxDebugPrimitives = 65536`; overflow logs `LOG_WARN` once per frame.
- All existing `DebugDraw::line/sphere/box/aabb` calls tessellate to line segments in the CPU accumulator.
- `DebugDraw::text(worldPos, str, color)` renders a screen-space billboard quad per glyph.

---

### R6 — `FpsCameraController` ECS Component + System *(Wave 1)*

**Owner:** Rendering Lead
**Depends on:** —

`rendering-pbr-and-camera.md §4.4` defines `FpsCameraController` as a registered ECS component with a system in the `Update` phase. The editor uses `EditorCamera`; gameplay uses raw `Transform` writes. Neither respects the designed interface.

**Deliverables:**
- `src/rendering/public/rendering/FpsCameraController.h`:
  ```cpp
  struct FpsCameraController {
      static constexpr core::ecs::ComponentTypeId kComponentId = 16; // next available
      float moveSpeed      { 5.0f };
      float lookSensitivity{ 0.1f };
      float yaw            { 0.0f };
      float pitch          { 0.0f }; // clamped ±89°
      bool  active         { true };
  };
  ```
- `FpsCameraSystem` registered in `Update` phase: reads `core::InputSystem::currentFrame()`, integrates yaw/pitch from `lookYawDelta` / `lookPitchDelta`, writes `Transform::rotation`; applies WASD movement along the camera's forward/right vectors.
- Registered in `Engine::init()` with `kComponentId = 16`; `ComponentEditorRegistry` widget added.
- `tests/rendering/FpsCameraSystemTests.cpp` (label: unit).

---

### R7 — Skinned Mesh Pipeline Stub *(Wave 1)*

**Owner:** Rendering Lead
**Depends on:** —

`rendering-mesh-material-shader.md §1.3` defines `VertexSkinned` (36 bytes) for forward-compatibility. `AnimationState` (id=10) is a stub. Phase 10 ships the pipeline plumbing so a future animation phase can drop in actual skeletal data without touching the renderer.

**Deliverables:**
- `shaders/skinned/SkinnedVS.hlsl` — identical to `OpaqueVS` plus bone transform lookup from a per-draw skinning buffer (up to 256 bones × `float4x4`).
- `MeshManager::uploadSkinned(CpuMesh&, span<Mat4> bindPose) → MeshHandle` — uploads `VertexSkinned` buffer + bind-pose matrices.
- `MeshRenderSystem`: if a `MeshHandle` references a skinned mesh and the entity has `AnimationState`, use the skinned PSO; otherwise use the standard opaque PSO.
- No actual bone animation logic — `AnimationState` fields drive the bone palette in a future phase.
- `tests/rendering/SkinnedMeshTests.cpp` (label: unit) — upload a 2-bone mesh; verify correct vertex buffer layout; verify PSO selection logic.

---

### T1 — CI Pipeline (GitHub Actions) *(Wave 1)*

**Owner:** Tools Lead
**Depends on:** —

`scope-testing.md` requires GitHub Actions: Debug + Release × Windows 2022 runner; unit tests blocking merge; integration tests gated on GPU runner.

**Deliverables:**
- `.github/workflows/ci.yml`:
  - Matrix: `{config: [Debug, Release]}` on `windows-2022` GitHub-hosted runner.
  - Steps: checkout → vcpkg install → cmake configure → cmake build → `ctest -L unit --output-on-failure`.
  - Failing unit tests block merge.
  - Integration tests (`ctest -L integration`) and benchmark suite (`ctest -L benchmark`) labelled as separate jobs with `continue-on-error: true` (require physical GPU; run on self-hosted runner when available).
- ASAN enabled for Debug config: add `/fsanitize=address` to `cmake/Warnings.cmake` under `CMAKE_BUILD_TYPE STREQUAL Debug`.
- PR status checks configured: `CI / build (Debug)` and `CI / build (Release)` required.
- Badge in top-level `README.md` (create if absent).

---

### T2 — Rendering Golden Image Tests *(Wave 3)*

**Owner:** Test Lead + Rendering Lead
**Depends on:** R1, R3

`testing-plan.md` specifies PNG golden files in `tests/rendering/goldens/` with ≤2% RMSE per channel.

**Deliverables:**
- `tests/rendering/goldens/` — committed PNG goldens for:
  - `clear_red.png` (already covered by `SmokeTest.cpp`).
  - `single_sphere_directional.png` — one sphere mesh, one directional light, no shadows, IBL.
  - `shadow_csm.png` — one mesh, one directional shadow-casting light, CSM enabled.
- `tests/rendering/support/GoldenCompare.cpp/.h` — loads actual readback pixels and expected PNG; asserts RMSE ≤ 2% per channel.
- `tests/rendering/GoldenTests.cpp` (label: integration) — runs on physical GPU runner; GTEST_SKIP if `!device.isValid()`.
- Updating a golden: PR description must explain why; diff of the PNG must be reviewed.

---

### T3 — Rendering Benchmark *(Wave 3)*

**Owner:** Test Lead + Rendering Lead
**Depends on:** R1

`testing-plan.md` requires four benchmark targets; `rendering_bench` is the only missing one.

**Deliverables:**
- `tests/benchmarks/rendering_bench.cpp`:
  - `BM_EmptyFrameClearPresent` — target ≤ 0.5 ms GPU at 1080p.
  - `BM_10kStaticMeshRecording` — CPU command recording, target ≤ 4 ms.
  - `BM_FrameGraphCompile` — target ≤ 0.05 ms.
  - `BM_LightCullSystem_64Lights` — target ≤ 0.2 ms CPU.
- `tests/benchmarks/baselines/desktop-mid/rendering_bench.json` — committed baseline from a first run.
- Wired into `tests/benchmarks/CMakeLists.txt` alongside the existing three targets.

---

### T4 — clang-format + clang-tidy CI Gates *(Wave 2)*

**Owner:** Tools Lead
**Depends on:** T1

`scope-testing.md` requires both as PR gates.

**Deliverables:**
- `.clang-format` at repo root — based on `Microsoft` style; key overrides: `ColumnLimit: 100`, `IndentWidth: 4`, `PointerAlignment: Left`.
- `.clang-tidy` at repo root — checks: `modernize-*`, `readability-*`, `performance-*`, `bugprone-*`; suppress `modernize-use-trailing-return-type` and `readability-magic-numbers` engine-wide.
- CI step: `cmake --build --preset build-debug --target clang-format-check` — fails if any file differs from formatted output.
- CI step: `cmake --build --preset build-debug --target clang-tidy` — fails on clang-tidy errors (warnings do not fail).
- Initial format pass applied to all source files; committed as a single formatting commit before the CI step is enforced.

---

## 4. Informational Note — E7 (Module Structure)

`tools-editor-and-profiler.md §7` placed the editor inside `src/tools/public/tools/editor/`. The actual implementation lives in `src/editor/` as a separate `engine_editor` static lib. This is architecturally superior and should be kept. The design doc should be updated to reflect the final structure — no code change required.

---

## 5. Owner Summary

| Owner | Tasks |
|-------|-------|
| Rendering Lead | A1, A3, R1, R2, R3\*, R5, R6, R7, T2\* |
| Tools Lead (build/assets) | R4, T1, T4, E2 |
| Tools Lead (editor) | E1, E3, E5, E6 |
| Physics Lead | E4\* |
| Networking Lead | N1, N3, N4\*, N5 |
| Core Lead | A2 |
| Test Lead | N4\*, T2\*, T3 |
| Team Leader | N2, E7 (doc update) |

\* Shared ownership — coordinate across teams.

---

## 6. Risk Register

| Risk | Mitigation |
|------|------------|
| R4 (texture cooking) is on the critical path for R3 and E3 | Start R4 in Wave 1 as early as possible; R3 and E3 can be scaffolded while R4 is in progress |
| R2 (shadow maps) is the largest single task (XL) | Timebox: ship directional CSM first; spot/point shadow maps can slip to a follow-up |
| E1 (ImGuizmo) may expose matrix decomposition edge cases with the row-major convention | Test gizmo with non-axis-aligned rotations before closing the task; the `row_major` HLSL bug history suggests this is a real risk |
| T1 (CI) requires GitHub Actions minutes; self-hosted GPU runner for integration tests may not be available | Gate integration tests as `continue-on-error`; do not block unit CI on GPU availability |
| A3 (Renderable consolidation) touches `MeshRenderSystem`, `SceneSerializer`, and the editor — broad blast radius | Do this early in Wave 1 and merge before dependent tasks (R1, E3) begin |
