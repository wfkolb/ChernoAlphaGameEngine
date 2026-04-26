# Task #45 — FlatShade Shaders and PSO

Status: Planned
Owner: Rendering Lead
Phase: 5
References: Phase_5/README.md, rendering-frame-graph.md, rendering-mesh-material-shader.md, src/rendering/internal/PipelineState.h

---

## 1. Purpose

A minimal pipeline state object (root signature + PSO) for rendering geometry
with normals-as-colour shading. No lighting calculation, no texture sampling,
no material system. The output looks like a cleanly lit object even though
it is just re-mapping the vertex normal into RGB colour space.

This is the lowest-complexity PSO that produces recognisable 3D output.

---

## 2. Shader Design

### 2.1 Vertex shader — `src/rendering/shaders/FlatShadeVS.hlsl`

Input layout matches `VertexStatic` (28 bytes, stride 28):

| Semantic | Format | Offset | Notes |
|---|---|---|---|
| POSITION | `R32G32B32_FLOAT` | 0 | float position[3] |
| NORMAL | `R10G10B10A2_UNORM` | 12 | packedNormal — hardware decodes to [0,1] float4 |
| TANGENT | `R10G10B10A2_UNORM` | 16 | packedTangent — declared but unused in VS |
| TEXCOORD0 | `R32G32_FLOAT` | 20 | uv[2] — declared but unused in VS |

Root constants at `register(b0)`: 16 × float = MVP matrix (row-major, 64 bytes, 16 DWORDs).

```hlsl
cbuffer ObjectCB : register(b0)
{
    float4x4 gMVP;  // row-major; applied as: pos_clip = mul(float4(pos,1), gMVP)
};

struct VsIn {
    float3 position : POSITION;
    float4 normal   : NORMAL;    // R10G10B10A2_UNORM decoded to [0,1] by hardware
    float4 tangent  : TANGENT;
    float2 uv       : TEXCOORD0;
};

struct VsOut {
    float4 posClip    : SV_POSITION;
    float3 worldNormal: NORMAL;
};

VsOut main(VsIn v)
{
    VsOut o;
    o.posClip     = mul(float4(v.position, 1.0f), gMVP);
    // Remap packed normal from [0,1] to [-1,1] and normalize.
    o.worldNormal = normalize(v.normal.xyz * 2.0f - 1.0f);
    return o;
}
```

Note: `gMVP` combines World × View × Proj. The demo passes a single pre-multiplied
matrix — no separate normal matrix needed here because we only use normals for colour,
not for correct lighting.

### 2.2 Pixel shader — `src/rendering/shaders/FlatShadePS.hlsl`

```hlsl
struct PsIn {
    float4 posClip    : SV_POSITION;
    float3 worldNormal: NORMAL;
};

float4 main(PsIn p) : SV_TARGET
{
    float3 n = normalize(p.worldNormal);
    return float4(n * 0.5f + 0.5f, 1.0f);
}
```

Output: each face gets a distinct colour based on its normal direction.
No branching, no texture lookups, no lighting.

### 2.3 Shader compilation

Compile offline with DXC at build time, same pattern as `OpaqueVS.hlsl` / `OpaquePS.hlsl`.
Output bytecode as `.cso` files embedded or loaded at runtime (follow the existing
shader loading pattern in `PipelineState.cpp`).

Shader model: `vs_6_0` / `ps_6_0`.

---

## 3. Root Signature

Single entry: root 32-bit constants at slot 0, `register(b0, space0)`, 16 DWORDs (the MVP matrix).

```
Root signature layout:
  [0] Root constants — 16 DWORDs — b0, space0
```

No descriptor tables, no samplers, no UAVs. This is intentionally minimal.

16 DWORDs is within the 64-DWORD per-root-signature hardware limit.

```cpp
// D3D12_ROOT_PARAMETER1 pseudo-code:
param.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
param.Constants.ShaderRegister  = 0;
param.Constants.RegisterSpace   = 0;
param.Constants.Num32BitValues  = 16;
param.ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
```

---

## 4. Pipeline State Object

Rasterizer state:
- `FillMode = D3D12_FILL_MODE_SOLID`
- `CullMode = D3D12_CULL_MODE_BACK`
- `FrontCounterClockwise = FALSE`

Depth-stencil state:
- `DepthEnable = TRUE`
- `DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL`
- `DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL`  ← reverse-Z

Blend state: opaque (no blending).

Render target count: 1.
- `RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM` (87)

Depth format: `DXGI_FORMAT_D32_FLOAT` (20).

Primitive topology: `D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE`.

---

## 5. Public API

### Header: `src/rendering/internal/FlatShadePipeline.h`

This is an **internal** header (under `src/rendering/internal/`) because it
uses DX12 types directly. It is not part of the public rendering API.

```cpp
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace engine::rendering {

struct FlatShadePipeline {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
};

// Create the root signature and PSO.
// device          : ID3D12Device* (not void* — this is an internal header).
// backBufferFormat: DXGI_FORMAT as uint32_t (87 = DXGI_FORMAT_R8G8B8A8_UNORM).
// depthFormat     : DXGI_FORMAT as uint32_t (20 = DXGI_FORMAT_D32_FLOAT).
FlatShadePipeline createFlatShadePipeline(
    ID3D12Device* device,
    uint32_t      backBufferFormat,
    uint32_t      depthFormat);

// Set the root signature, PSO, VB, IB, and root constants on cmdList.
// Call immediately before DrawIndexedInstanced.
// mvp: pointer to 16 floats, row-major World*View*Proj matrix.
void bindFlatShade(
    const FlatShadePipeline& pipeline,
    ID3D12GraphicsCommandList* cmdList,
    const void*  vbView,   // D3D12_VERTEX_BUFFER_VIEW*
    const void*  ibView,   // D3D12_INDEX_BUFFER_VIEW*
    const float  mvp[16]);

} // namespace engine::rendering
```

### Implementation: `src/rendering/FlatShadePipeline.cpp`

- `createFlatShadePipeline`: load compiled `.cso` shader bytecode, build root
  signature via `D3D12SerializeVersionedRootSignature`, create PSO with
  `CreateGraphicsPipelineState`. Follow the exact same error-handling pattern
  as `createOpaquePassPipeline` in `PipelineState.cpp`.

- `bindFlatShade`: call `SetGraphicsRootSignature`, `SetPipelineState`,
  `IASetVertexBuffers`, `IASetIndexBuffer`, `IASetPrimitiveTopology`,
  `SetGraphicsRoot32BitConstants` (16 values from `mvp`, offset 0).

---

## 6. CMake

`FlatShadePipeline.cpp` is under `src/rendering/` and will be picked up
automatically by the `GLOB_RECURSE` in `src/rendering/CMakeLists.txt`.

The `.hlsl` shaders need DXC compile rules. Follow the existing pattern
for `OpaqueVS.hlsl` / `OpaquePS.hlsl` in `src/rendering/CMakeLists.txt`.
Output `.cso` files to `${CMAKE_BINARY_DIR}/shaders/`.

---

## 7. Acceptance Criteria

- `createFlatShadePipeline` succeeds on a machine with a DX12 device.
- `bindFlatShade` + `DrawIndexedInstanced(36, 1, 0, 0, 0)` on the unit cube
  produces a coloured triangle mesh with no GPU validation errors.
- No DX12 types appear in any public header (`src/rendering/public/`).
- Shader compilation succeeds at build time with `vs_6_0` / `ps_6_0` with
  zero warnings under `/WX`.
