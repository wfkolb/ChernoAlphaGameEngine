# Rendering: Frame Graph

Status: Approved (Phase 2)
Owner: Rendering Lead
Task: #7
References: architecture.md §2, rendering-window-and-dx12.md, scope-rendering.md

---

## 1. Purpose

The frame graph is a per-frame declarative description of render passes and the resources they read and write. It drives:

- Automatic DX12 resource barrier insertion (no manual `ResourceBarrier` calls outside the frame graph).
- Transient resource lifetime management (resources that live only within a single frame).
- PSO cache keying.
- Deterministic pass execution order.

The frame graph is compiled and executed once per frame. "Compile" means resolving resource states and inserting barriers; it is very cheap (< 0.05 ms target per `architecture.md`).

---

## 2. Resources

### 2.1 Resource classification

| Kind | Lifetime | Backing | Examples |
|---|---|---|---|
| Transient | Single frame | Frame-arena-backed, aliased | Shadow map, G-buffer passes |
| Persistent | Multi-frame | Committed heap resource | Back buffer, depth buffer, loaded textures |
| Imported | External to frame graph | Caller provides handle | Back buffer RTV, depth DSV from GpuDevice |

### 2.2 ResourceHandle

```cpp
// Opaque handle to a frame-graph-managed resource.
// Index into the frame graph's resource table; invalid == 0xFFFF
struct ResourceHandle {
    uint16_t id{ 0xFFFFu };
    bool isValid() const noexcept { return id != 0xFFFFu; }
};
```

Consumers hold `ResourceHandle`; the frame graph holds the actual `ID3D12Resource*`.

### 2.3 Resource descriptors

Passes declare resources by description, not by pointer:

```cpp
struct TextureDesc {
    uint32_t          width, height;
    uint32_t          mipLevels  = 1;
    DXGI_FORMAT       format;
    D3D12_RESOURCE_FLAGS flags   = D3D12_RESOURCE_FLAG_NONE;
    float             clearColor[4] = {0,0,0,1};  // for render targets
    float             clearDepth    = 0.0f;         // for depth (reverse-Z: far = 0)
    uint8_t           clearStencil  = 0;
};
```

For persistent resources, the frame graph stores the `ID3D12Resource*` provided at import time. For transient resources, the frame graph creates and destroys them each frame (or aliases within the frame arena — v1 defers aliasing to v2 if needed).

---

## 3. Pass Declaration API

### 3.1 Adding a pass

```cpp
class FrameGraph {
public:
    class PassBuilder {
    public:
        ResourceHandle read (ResourceHandle h, D3D12_RESOURCE_STATES requiredState);
        ResourceHandle write(ResourceHandle h, D3D12_RESOURCE_STATES requiredState);
        ResourceHandle create(const TextureDesc& desc, std::string_view name);
        ResourceHandle import(ID3D12Resource* resource, D3D12_RESOURCE_STATES currentState, std::string_view name);
    };

    using ExecuteFn = std::function<void(ID3D12GraphicsCommandList*, const PassResources&)>;

    void addPass(std::string_view name, std::function<void(PassBuilder&)> setup, ExecuteFn execute);

    // Import the back buffer and depth buffer before building passes.
    ResourceHandle importBackBuffer(ID3D12Resource* backBuf, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                                    D3D12_RESOURCE_STATES currentState);
    ResourceHandle importDepthBuffer(ID3D12Resource* depth, D3D12_CPU_DESCRIPTOR_HANDLE dsv);

    void compile();    // resolve barriers; cull unused passes (not in v1 — all passes execute)
    void execute(ID3D12GraphicsCommandList* cmdList);
    void reset();      // call at start of each frame before adding passes
};
```

`PassResources` provides resolved handles:
```cpp
class PassResources {
public:
    ID3D12Resource*             getResource(ResourceHandle h) const;
    D3D12_CPU_DESCRIPTOR_HANDLE getRtv(ResourceHandle h)      const;
    D3D12_CPU_DESCRIPTOR_HANDLE getDsv(ResourceHandle h)      const;
    D3D12_GPU_DESCRIPTOR_HANDLE getSrv(ResourceHandle h)      const;
    D3D12_GPU_DESCRIPTOR_HANDLE getUav(ResourceHandle h)      const;
};
```

### 3.2 Example: a simple clear pass

```cpp
ResourceHandle hBackBuf = fg.importBackBuffer(device.currentBackBuffer(), device.currentBackBufferRtv(),
                                               D3D12_RESOURCE_STATE_PRESENT);
fg.addPass("Clear",
    [&](FrameGraph::PassBuilder& b) {
        hBackBuf = b.write(hBackBuf, D3D12_RESOURCE_STATE_RENDER_TARGET);
    },
    [hBackBuf](ID3D12GraphicsCommandList* cmd, const PassResources& res) {
        const float clearColor[] = {0.1f, 0.1f, 0.1f, 1.0f};
        cmd->ClearRenderTargetView(res.getRtv(hBackBuf), clearColor, 0, nullptr);
    });
```

The pass setup lambda captures the handles by reference and may reassign them (a write to a resource conceptually produces a new "version"). The execute lambda captures by value and may only use resolved handles.

### 3.3 ImGui pass integration

The editor (Tools Lead, DevRel only) adds an ImGui pass at the very end of the frame graph:

```cpp
fg.addPass("ImGui",
    [&](FrameGraph::PassBuilder& b) {
        hBackBuf = b.write(hBackBuf, D3D12_RESOURCE_STATE_RENDER_TARGET);
    },
    [hBackBuf](ID3D12GraphicsCommandList* cmd, const PassResources& res) {
        auto rtv = res.getRtv(hBackBuf);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);
    });
```

The rendering module provides the descriptor heap slot for the ImGui font texture (one SRV in the main CBV/SRV/UAV heap). The editor must not create its own heap.

---

## 4. Automatic Barrier Insertion

### 4.1 Compile phase

During `compile()`, the frame graph walks all passes in registration order and, for each resource, tracks the last-known state. When a pass declares a different required state than the resource's current state, the frame graph inserts a `D3D12_RESOURCE_BARRIER` of type `TRANSITION` immediately before that pass's execute lambda.

Barriers are batched per pass boundary using `ID3D12GraphicsCommandList::ResourceBarrier` with an array of transitions.

### 4.2 End-of-frame cleanup

After all passes execute, transition all imported resources back to their expected state (e.g., back buffer → `PRESENT`). Transient resources are left in whatever state the last pass left them — they are discarded.

### 4.3 Constraints (v1)

- No split barriers in v1. All barriers are `BEGIN + END` in one call (standard transition).
- No UAV barrier support in v1 (no compute passes that write UAVs and read them in the same frame).
- No aliasing barriers in v1.

---

## 5. PSO Cache

### 5.1 Cache key

```cpp
struct PsoKey {
    uint64_t shaderHash;          // hash of VS + PS bytecodes
    uint32_t renderTargetFormats; // packed DXGI_FORMAT per slot (4 bits each, 8 slots = 32 bits)
    DXGI_FORMAT depthFormat;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology;
    uint8_t  numRenderTargets;
    uint8_t  sampleCount;
    // equality and hash operators
};
```

`shaderHash` is a `std::hash` over the concatenated VS + PS bytecode. The cache invalidates on hot-reload by recomputing the hash.

### 5.2 Cache storage

```cpp
std::unordered_map<PsoKey, ComPtr<ID3D12PipelineState>, PsoKeyHash> psoCache_;
```

`createOrGetPso(const PsoKey&, const D3D12_GRAPHICS_PIPELINE_STATE_DESC&)` — returns a cached PSO or creates and caches a new one.

Cache is not serialized to disk in v1 (no PSO library / DX12 cached blob). Hot-reload in DevRel recreates affected PSOs immediately; a 1-2 frame hitch is acceptable (within the ≤ 250 ms shader hot-reload budget in scope-rendering.md).

### 5.3 Root signature derivation

Root signatures are created from DXC shader reflection (see rendering-mesh-material-shader.md §4). One root signature per unique binding layout; the PSO cache maps PSO keys to (root signature + PSO) pairs. In v1, a single "standard" root signature covers all rasterization passes.

---

## 6. Per-Frame Fence Pattern Integration

The frame graph does not own fences — those are in `GpuDevice`. The frame graph's `execute()` is called between `GpuDevice::beginFrame()` and `GpuDevice::endFrame()`:

```
GpuDevice::beginFrame()
  → wait for oldest in-flight frame
  → reset command allocator
  → reset + open command list
FrameGraph::reset()
  → clear pass list, reset resource table
[app builds passes]
FrameGraph::compile()
  → resolve barriers
FrameGraph::execute(cmdList)
  → record all pass lambdas with barriers
GpuDevice::endFrame()
  → close command list
  → ExecuteCommandLists
  → Signal fence
  → Present
```

The command list pointer passed to `FrameGraph::execute()` is the same one `GpuDevice::nativeCommandList()` returns.

---

## 7. Pass Execution Details

### 7.1 Render target setup

Passes that write to render targets are responsible for calling `OMSetRenderTargets` themselves inside the execute lambda. The frame graph does not set render targets automatically — it only transitions resource states.

### 7.2 Viewport and scissor

Each pass sets its own viewport and scissor rect. The frame graph provides a helper:

```cpp
void setFullscreenViewportScissor(ID3D12GraphicsCommandList* cmd, uint32_t width, uint32_t height);
```

### 7.3 Pass ordering

Passes execute in registration order. In v1 there is no automatic culling of unreferenced passes — all registered passes execute. (Culling is a v2 optimization.)

---

## 8. File Layout

```
src/rendering/
├── public/rendering/
│   ├── FrameGraph.h         — FrameGraph class, PassBuilder, ResourceHandle, PassResources
│   ├── FrameGraph.inl       — template implementations for addPass<>
│   └── TextureDesc.h        — TextureDesc struct
└── internal/
    ├── FrameGraphImpl.h     — internal resource tables, barrier lists
    └── PsoCache.h           — PsoKey, PsoCache
```

`FrameGraph.h` may expose `D3D12_CPU_DESCRIPTOR_HANDLE` in its public interface (a plain struct). `ID3D12Resource*` is never exposed in the public header; only `ResourceHandle` is.
