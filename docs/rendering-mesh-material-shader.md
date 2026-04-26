# Rendering: Mesh, Material, and Shader Pipeline

Status: Approved (Phase 2)
Owner: Rendering Lead
Task: #8
References: architecture.md §2 §5, rendering-frame-graph.md, scope-rendering.md, tools-build-and-asset-pipeline.md

---

## 1. Vertex Layouts

Three distinct vertex buffer layouts are defined. Shaders select the correct input layout via PSO configuration.

### 1.1 Positions-only (`VERTEX_LAYOUT_POSITION`)

Used for shadow passes, depth pre-pass, debug geometry.

```cpp
struct VertexPosition {
    float x, y, z;                      // 12 bytes
};
// Total: 12 bytes/vertex
```

HLSL input:
```hlsl
struct VSInput_Position {
    float3 position : POSITION;
};
```

### 1.2 Static mesh (`VERTEX_LAYOUT_STATIC`)

Used for all opaque and alpha-tested geometry.

```cpp
struct VertexStatic {
    float    position[3];     // 12 bytes
    uint32_t packedNormal;    // 10/10/10/2 UNORM, w unused — 4 bytes
    uint32_t packedTangent;   // 10/10/10/2 UNORM, w = sign(bitangent) in bit31 — 4 bytes
    float    uv[2];           // 8 bytes
};
// Total: 28 bytes/vertex, 4-byte aligned
```

`packedNormal` and `packedTangent` use `DXGI_FORMAT_R10G10B10A2_UNORM`. The bitangent is reconstructed in the shader as `cross(normal, tangent.xyz) * tangent.w`.

HLSL input:
```hlsl
struct VSInput_Static {
    float3 position : POSITION;
    float3 normal   : NORMAL;    // decoded from packed in VS
    float4 tangent  : TANGENT;   // xyz = tangent, w = sign
    float2 uv       : TEXCOORD0;
};
```

### 1.3 Skinned mesh (`VERTEX_LAYOUT_SKINNED`)

Extends static mesh with bone indices and weights. Out-of-scope for v1 animation, but the layout is defined now so the asset format is forward-compatible.

```cpp
struct VertexSkinned {
    VertexStatic base;           // 28 bytes
    uint8_t  boneIndices[4];     //  4 bytes
    uint8_t  boneWeights[4];     //  4 bytes (UNORM8, last weight = 1 - sum of others)
};
// Total: 36 bytes/vertex
```

### 1.4 Index buffer

Always `DXGI_FORMAT_R32_UINT` (uint32_t). 16-bit indices are not used; the asset importer upcasts. This avoids conditional index format handling in the renderer.

---

## 2. GPU Buffer Management

### 2.1 Default heap (persistent geometry)

Static mesh data lives in `D3D12_HEAP_TYPE_DEFAULT` resources. Upload is done once at asset load time via a copy queue operation (or a staging buffer on the direct queue in v1 before a copy queue is wired).

Upload sequence:
1. Allocate a `D3D12_HEAP_TYPE_UPLOAD` staging buffer of the same size.
2. `memcpy` vertex/index data into the staging buffer's mapped range.
3. `CopyBufferRegion` from staging to default heap on the copy/graphics command list.
4. Transition default heap buffer from `COPY_DEST` to `VERTEX_AND_CONSTANT_BUFFER` or `INDEX_BUFFER`.
5. Track staging buffer lifetime with a fence — free it after the GPU signals completion.

### 2.2 Per-frame upload heap (dynamic constants)

A `D3D12_HEAP_TYPE_UPLOAD` ring buffer is allocated per frame slot (indexed by `frameIndex % kBackBufferCount`). Size: 4 MB per frame (configurable via `[render].uploadHeapSizeMb`). Used for:
- Per-frame constant buffers (camera, time, light data).
- Per-draw object constants (model matrix, material index).

The ring buffer is written via a persistent `Map()` with `NULL` read range. It is never unmapped until device shutdown.

Alignment: constant buffer views require 256-byte alignment; the ring buffer allocator rounds up to 256 bytes per allocation.

---

## 3. Descriptor Heaps

v1 uses two shader-visible descriptor heaps, created once at device init and never resized.

| Heap | Type | Shader-visible | Size | Purpose |
|---|---|---|---|---|
| `mainHeap_` | `CBV_SRV_UAV` | Yes | 4096 descriptors | All textures (SRV), per-draw CBVs, UAVs |
| `samplerHeap_` | `SAMPLER` | Yes | 64 descriptors | All samplers |

Non-shader-visible heaps (CPU-only):
| Heap | Type | Size |
|---|---|---|
| `rtvHeap_` | `RTV` | 32 (back buffers + render targets) |
| `dsvHeap_` | `DSV` | 8 (depth buffers) |

### 3.1 Main heap layout (static assignments)

```
[0]         : null SRV (used for bindless padding)
[1..2047]   : texture SRVs (assigned at load time from a free-list)
[2048..3071]: per-frame CBV ring (written each frame)
[3072..4095]: UAV and miscellaneous
```

The ImGui font texture occupies one slot in the texture SRV range (assigned at editor init).

### 3.2 Sampler heap layout

Pre-built at device init; not dynamic in v1.

```
[0]  : LinearWrap   (min/mag/mip = linear, wrap UVW)
[1]  : LinearClamp
[2]  : PointWrap
[3]  : PointClamp
[4]  : AnisotropicWrap (max anisotropy = 8, from config)
[5]  : ShadowPCF     (comparison, linear, border color = 1.0)
[6..63]: reserved
```

---

## 4. Root Signature

A single "standard" root signature covers all v1 rasterization passes. It is created via explicit C++ API (not DXC reflection) to ensure stability.

```
Slot 0: Root constants (32 × DWORD)
         — objectIndex (draw call payload; indexes into per-draw data in mainHeap_)
         — other per-draw constants as needed

Slot 1: CBV  — per-frame constant buffer (b0, space0)
               Camera, time, light count, shadow matrices

Slot 2: Descriptor table — SRV range, mainHeap_[0..2047]  (t0..t2047, space0)
         — bindless textures

Slot 3: Descriptor table — Sampler range, samplerHeap_[0..63] (s0..s63, space0)

Flags: ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | DENY_HULL/DOMAIN/GEOMETRY_SHADER_ROOT_ACCESS
```

Root constant at slot 0 is used to pass an object index. The VS/PS reads from a structured buffer (via the SRV range) to get per-object data by index (bindless draw data pattern).

---

## 5. HLSL Shader Pipeline

### 5.1 Directory layout

```
engine/shaders/
├── common/
│   ├── RootSignature.hlsli    — root signature macro used by every shader
│   ├── VertexLayouts.hlsli    — input struct definitions
│   ├── Constants.hlsli        — kPi, depth convention notes, etc.
│   └── Packing.hlsli          — 10/10/10/2 unpack helpers
├── opaque/
│   ├── OpaqueVS.hlsl
│   └── OpaquePS.hlsl
├── shadow/
│   ├── ShadowVS.hlsl
│   └── ShadowPS.hlsl
└── debug/
    ├── DebugLineVS.hlsl
    └── DebugLinePS.hlsl
```

All shaders include `common/RootSignature.hlsli` at the top. No relative path tricks; include paths are configured by CMake (`/I engine/shaders`).

### 5.2 DXC invocation flags

Compile-time (asset build / CI):
```
dxc.exe -T vs_6_6 -E VSMain -Fo output.dxv
        -I engine/shaders
        -WX -Ges             (warnings as errors, strict mode)
        -Qstrip_reflect      (strip reflection from output, keep separate)
        -Qstrip_debug        (strip debug in release)
        -O3
        /Fd output.pdb       (DevRel only)
```

DevRel runtime hot-reload:
- Same flags but `-O0` for faster recompilation.
- File watcher monitors `engine/shaders/` for `.hlsl` / `.hlsli` changes.
- On change, recompile affected shaders (tracked by dependency `.d` files generated by DXC `-MD` flag).
- Recompile latency target: ≤ 250 ms from file save to next rendered frame.

### 5.3 Shader reflection

DXC generates reflection via `IDxcUtils::CreateReflection` from the separate reflection blob (`-Qstrip_reflect` separates it). Reflection is used at load time to:
- Validate that the shader matches the expected root signature slot bindings.
- Populate `PsoKey::shaderHash` from the bytecode.

Reflection is NOT used to derive the root signature — the root signature is fixed (§4) and manually created. This avoids per-shader RS creation overhead and keeps the RS stable across shader hot-reloads.

### 5.4 Shader bytecode storage

Compiled shader bytecode is stored in `.dxv` files (DX Vertex), `.dxp` (DX Pixel), etc., alongside the source. At runtime, `core::fs::FileHandle::readAll()` loads the bytecode; the `D3D12_SHADER_BYTECODE` struct points directly into the loaded buffer (zero-copy via memory-mapped file where possible).

---

## 6. Material System

### 6.1 Material data

```cpp
// PBR material parameters stored per material in a GPU structured buffer
struct GpuMaterial {
    uint32_t albedoTextureIndex;     // index into mainHeap_ SRV range; 0xFFFF = no texture
    uint32_t normalTextureIndex;
    uint32_t metallicRoughnessIndex; // packed: R=metallic, G=roughness (glTF convention)
    uint32_t emissiveTextureIndex;
    float    albedoFactor[4];        // base color multiplier
    float    metallicFactor;
    float    roughnessFactor;
    float    emissiveFactor[3];
    float    pad_;
};
static_assert(sizeof(GpuMaterial) == 64);
```

All materials are packed into a single `D3D12_HEAP_TYPE_DEFAULT` structured buffer. Per-draw root constants carry the material index; the PS reads `GpuMaterial` from the buffer by index.

### 6.2 Material handle

```cpp
struct MaterialHandle { uint16_t index; };
constexpr MaterialHandle kInvalidMaterial{ 0xFFFFu };
```

Materials are created by the renderer when the asset system calls `GpuDevice::uploadMaterial(const GpuMaterial&)`. The renderer manages the material buffer as a growing pool.

### 6.3 Mesh handle

```cpp
struct MeshHandle { uint32_t id; };
constexpr MeshHandle kInvalidMesh{ 0xFFFF'FFFFu };
```

A `MeshHandle` identifies a (vertex buffer, index buffer, vertex layout, index count, AABB) tuple stored in the renderer's mesh registry.

---

## 7. Renderable Component

Registered by the rendering module at startup (ecs::World is provided by core):

```cpp
struct Renderable {
    MeshHandle     mesh     { kInvalidMesh };
    MaterialHandle material { kInvalidMaterial };
    bool           castShadow { true };
    bool           receiveShadow { true };
};
```

The culling system iterates `View<Transform, Renderable>()` each frame and submits visible instances to draw lists.
