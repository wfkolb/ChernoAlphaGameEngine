# Phase 5: Spinning Model Demo

Status: Planned
Generated: 2026-04-26

---

## Goal

Render a loaded glTF model spinning in a window. No lighting, no material system.
Flat shading (normals-as-colour) gives a visually clear 3D result without any light sources.

---

## Deliverables

| Task | File | Owner | Depends on |
|------|------|-------|------------|
| [#44 EassetLoader](task-44-easset-loader.md) | `src/tools/EassetLoader.h/cpp` | Tools Lead | — |
| [#45 FlatShade PSO](task-45-flatshade-pipeline.md) | `src/rendering/shaders/FlatShade*.hlsl`, `FlatShadePipeline.h/cpp` | Rendering Lead | — |
| [#46 FlatShade Pass](task-46-flatshade-pass.md) | `addFlatShadePass()` free function | Rendering Lead | #45 |
| [#47 SpinDemo App](task-47-spin-demo.md) | `src/demos/SpinDemo.cpp` | Team Leader | #44, #46 |
| [#48 Tests](task-48-tests.md) | `EassetLoaderTest.cpp`, `SpinDemoTest.cpp` | Test Lead | #44 |

Tasks #44 and #45 are independent and can be built in parallel.
The Networking Lead has no role in this phase.

---

## What Already Exists

The following infrastructure is complete and must be used as-is:

| Existing piece | Location | Used by |
|---|---|---|
| `MeshManager::uploadStatic(vertices, indices)` | `src/rendering/MeshManager.h` | #47 |
| `MeshManager::vertexBufferView / indexBufferView / indexCount` | `src/rendering/MeshManager.h` | #46 |
| `FrameGraph::addPass / importBackBuffer / importDepthBuffer / compile / execute / reset` | `src/rendering/FrameGraph.h` | #46, #47 |
| `setFullscreenViewportScissor(cmdList, w, h)` | `src/rendering/FrameGraph.h` | #46 |
| `Camera` free functions: `cameraViewMatrix`, `cameraProjMatrix` | `src/rendering/Camera.h` | #47 |
| `Transform::toMatrix()` | `src/core/math/Transform.h` | #47 |
| `Quat::fromAxisAngle` / `mat4 rotation` | `src/core/math/Quat.h` | #47 |
| `AssetImporter::importGltf(source, output)` | `src/tools/AssetImporter.h` | #47 |
| `VertexStatic` (28 bytes) | `src/rendering/Mesh.h` | #44, #45 |
| `Window`, `GpuDevice` | `src/rendering/` | #47 |

---

## Architecture Sketch

```
.glb file
   │
   ▼  importGltf() [AssetImporter — already exists]
.easset file
   │
   ▼  loadEasset() [Task #44 — Tools Lead]
CpuMesh { vector<VertexStatic>, vector<uint32_t> }
   │
   ▼  MeshManager::uploadStatic() [already exists]
MeshHandle (GPU vertex + index buffers)
   │
   ▼  addFlatShadePass() [Task #46 — Rendering Lead]
   │    uses FlatShadePipeline [Task #45 — Rendering Lead]
   ▼
FrameGraph::compile() + execute()
   │
   ▼
Present to screen
```

---

## Visual Output

Each pixel is shaded as `float4(normalize(worldNormal) * 0.5 + 0.5, 1.0)`.
This maps:
- +X normal → red tint
- +Y normal → green tint
- +Z normal → blue tint

No light sources needed. The model reads clearly as 3D because face normals vary across the surface.

---

## Key Constraints (carry forward from all phases)

- No DX12/Win32 types in public headers — void*, uint64_t, uintptr_t only.
- Math: RH, Y-up, +Z forward, row-major Mat4, row-vector convention (v' = v * M).
- Depth: reverse-Z — near = 1.0, far = 0.0; depth compare = GREATER_EQUAL.
- No exceptions anywhere in engine code.
- All editor / DevRel code inside `#ifdef ENGINE_DEVREL`.
- `ctest -L unit` must continue to pass 108/108 after this phase.
