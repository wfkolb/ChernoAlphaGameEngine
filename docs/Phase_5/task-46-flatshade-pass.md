# Task #46 — FlatShade FrameGraph Pass

Status: Planned
Owner: Rendering Lead
Phase: 5
References: Phase_5/README.md, rendering-frame-graph.md, task-45-flatshade-pipeline.md, src/rendering/FrameGraph.h, src/rendering/MeshManager.h

---

## 1. Purpose

Wire the `FlatShadePipeline` (Task #45) into the FrameGraph as a single
render pass. The result is a free function `addFlatShadePass()` that the
SpinDemo (Task #47) calls each frame to schedule the draw.

---

## 2. Public API

### Header: `src/rendering/public/rendering/FlatShadePass.h`

This is a **public** header. It must not include any DX12 headers.
All DX12 types are hidden behind void* / uint64_t.

```cpp
#pragma once
#include <cstdint>

namespace engine::rendering {

class  FrameGraph;
struct MeshManager;
struct FlatShadePipeline;   // forward — defined in internal/FlatShadePipeline.h

// Add a flat-shade draw pass to the frame graph for the given mesh.
// fg         : frame graph being recorded for this frame.
// mm         : mesh manager; provides vertexBufferView, indexBufferView, indexCount.
// pipeline   : compiled FlatShadePipeline (root sig + PSO).
// mvp        : 16 floats, row-major World*View*Proj, caller owns storage.
// backBuffer : resource handle returned by fg.importBackBuffer().
// depthBuffer: resource handle returned by fg.importDepthBuffer().
void addFlatShadePass(
    FrameGraph&               fg,
    const MeshManager&        mm,
    const FlatShadePipeline&  pipeline,
    const float               mvp[16],
    uint64_t                  backBuffer,
    uint64_t                  depthBuffer);

} // namespace engine::rendering
```

`uint64_t` for resource handles matches the existing FrameGraph handle type
(see `FrameGraph.h` — `using ResourceHandle = uint64_t`).

### Implementation: `src/rendering/FlatShadePass.cpp`

```cpp
#include <rendering/FlatShadePass.h>
#include <rendering/FrameGraph.h>
#include <rendering/MeshManager.h>
#include <rendering/internal/FlatShadePipeline.h>
#include <array>
#include <cstring>   // std::memcpy

namespace engine::rendering {

void addFlatShadePass(
    FrameGraph&              fg,
    const MeshManager&       mm,
    const FlatShadePipeline& pipeline,
    const float              mvp[16],
    uint64_t                 backBuffer,
    uint64_t                 depthBuffer)
{
    // Copy MVP into pass-local storage captured by the lambda.
    // The caller's mvp pointer may be on the stack and must not be captured
    // by reference across the frame graph deferred execution boundary.
    std::array<float, 16> mvpCopy;
    std::memcpy(mvpCopy.data(), mvp, sizeof(mvpCopy));

    fg.addPass("FlatShade",
        [backBuffer, depthBuffer](FrameGraph::PassBuilder& b) {
            b.write(backBuffer);
            b.readDepth(depthBuffer);
        },
        [&mm, &pipeline, mvpCopy](void* cmdList, uint32_t w, uint32_t h) {
            setFullscreenViewportScissor(cmdList, w, h);
            bindFlatShade(
                pipeline,
                static_cast<ID3D12GraphicsCommandList*>(cmdList),
                &mm.vertexBufferView,
                &mm.indexBufferView,
                mvpCopy.data());
            static_cast<ID3D12GraphicsCommandList*>(cmdList)
                ->DrawIndexedInstanced(mm.indexCount, 1, 0, 0, 0);
        });
}

} // namespace engine::rendering
```

Notes:
- `setFullscreenViewportScissor` is already declared in `FrameGraph.h` as a free function.
- `bindFlatShade` is declared in `internal/FlatShadePipeline.h`.
- The execute lambda casts `void* cmdList` to `ID3D12GraphicsCommandList*`. This is
  acceptable because the execute lambda is called by FrameGraph::execute(), which already
  lives in the rendering module (non-public compilation unit) and has access to DX12 types.
- `MeshManager::vertexBufferView` and `::indexBufferView` are `D3D12_VERTEX_BUFFER_VIEW`
  and `D3D12_INDEX_BUFFER_VIEW` values (internal structs). The `bindFlatShade` signature
  takes `const void*` to avoid leaking DX12 types through the public API boundary — the
  cast happens inside `bindFlatShade` in `FlatShadePipeline.cpp`.

---

## 3. Pass Declaration vs Execution

`FrameGraph::addPass` takes two lambdas:

| Lambda | When called | Purpose |
|---|---|---|
| Setup (`PassBuilder&`) | compile time | Declare reads/writes so the frame graph can build the dependency graph and insert barriers. |
| Execute (`void* cmdList, w, h`) | execute time | Record GPU commands. |

The setup lambda declares `write(backBuffer)` — this tells the frame graph that this
pass produces the final colour output and causes it to insert a transition barrier from
`PRESENT` → `RENDER_TARGET` before the pass and `RENDER_TARGET` → `PRESENT` after.

`readDepth(depthBuffer)` declares a depth-stencil read/write dependency.

---

## 4. CMake

`FlatShadePass.cpp` lives under `src/rendering/` and is picked up automatically
by the `GLOB_RECURSE` in `src/rendering/CMakeLists.txt`.

The public header lives under `src/rendering/public/rendering/` and is available
to all targets that link `engine::rendering`.

---

## 5. Acceptance Criteria

- `addFlatShadePass` compiles with no DX12 headers included in `FlatShadePass.h`.
- A frame graph compiled with only `addFlatShadePass` produces one render pass with
  correct before/after barriers on the back buffer (verify via DX12 debug layer — no
  validation errors or warnings on a machine with a GPU).
- `DrawIndexedInstanced(36, 1, 0, 0, 0)` on the unit cube mesh produces a coloured
  triangle output (same acceptance as Task #45).
- `setFullscreenViewportScissor` is not called in setup (only in execute).
