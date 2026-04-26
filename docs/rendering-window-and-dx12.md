# Rendering: Window and DX12 Context

Status: Approved (Phase 2)
Owner: Rendering Lead
Task: #6
References: architecture.md §1-3, module-structure.md §3.2, scope-rendering.md

---

## 1. Win32 Window

### 1.1 Window class registration

A single `WNDCLASSEXW` is registered once per process under the name `L"EngineWindow"`, guarded by a `static std::once_flag` inside `Window::create()`.

Required fields:
- `style = CS_HREDRAW | CS_VREDRAW`
- `lpfnWndProc = Window::staticWndProc` — a static forwarder that reads the `Window*` from `GWLP_USERDATA` and calls the instance method.
- `hCursor = LoadCursorW(nullptr, IDC_ARROW)`
- `hIcon = nullptr` (v1 ships no custom icon)
- `hInstance` = the `HINSTANCE` passed into `WinMain` (stored in `app::Engine`)

### 1.2 HWND creation

```
CreateWindowExW(
    WS_EX_APPWINDOW,
    L"EngineWindow",
    title,
    WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT, CW_USEDEFAULT,
    adjustedWidth, adjustedHeight,   // AdjustWindowRectEx from client size
    nullptr, nullptr, hInstance, this
)
```

Initial client size comes from `[render].width` and `[render].height` in `engine.toml` (defaults 1280 × 720). `AdjustWindowRectEx` converts client rect to window rect before passing to `CreateWindowExW`.

After creation: `SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this))`, then `ShowWindow(hwnd, SW_SHOWDEFAULT)` + `UpdateWindow(hwnd)`.

### 1.3 Raw input registration

Immediately after `ShowWindow`, register two `RAWINPUTDEVICE` entries:

| Device | usUsagePage | usUsage | dwFlags |
|---|---|---|---|
| Mouse | 1 | 2 | `RIDEV_INPUTSINK` |
| Keyboard | 1 | 6 | `RIDEV_INPUTSINK` |

`RIDEV_INPUTSINK` ensures input arrives even when the window is not foreground (needed for the editor in DevRel builds).

### 1.4 Message pump

The pump lives in `Engine::run()` on the main thread:

```cpp
while (!quit_) {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { quit_ = true; break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (!quit_) tickFrame();
}
```

`WndProc` message handling:

| Message | Action |
|---|---|
| `WM_DESTROY` | `PostQuitMessage(0)` |
| `WM_SIZE` | Post `core::events::WindowResized{w, h}` on event bus; do NOT resize swapchain inline |
| `WM_ACTIVATE` | Post `core::events::WindowFocusChanged{focused}` |
| `WM_INPUT` | Call `GetRawInputData`; translate to `core::events::RawInputEvent`; post on event bus |
| `WM_SYSKEYDOWN` (Alt+Enter) | Reserved for future fullscreen toggle; stubbed as `DefWindowProcW` in v1 |

All other messages go to `DefWindowProcW`.

### 1.5 HWND lifetime and ownership

`Window` is an RAII type. Its destructor calls `DestroyWindow(hwnd_)`. The swapchain **must** be destroyed before `DestroyWindow` — `GpuDevice` holds the swapchain and must be destroyed first. `BootstrapOrder.cpp` enforces this shutdown order.

Window class unregistration (`UnregisterClassW`) happens in a static destructor guarded by the same `once_flag`.

### 1.6 Public API

No Win32 types appear in `Window.h`. Forward-declare `struct HWND__` if the escape hatch is ever needed; prefer `void*`.

```cpp
// src/rendering/public/rendering/Window.h
class Window {
public:
    struct Desc { uint32_t width; uint32_t height; std::wstring_view title; };

    static Window create(const Desc& desc);

    void*    nativeHandle() const noexcept;   // returns HWND; cast only in rendering internals
    uint32_t clientWidth()  const noexcept;
    uint32_t clientHeight() const noexcept;
    bool     wantsClose()   const noexcept;   // set when WM_QUIT is processed

    ENGINE_NO_COPY(Window);
    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;
    ~Window();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

---

## 2. DXGI Factory and Adapter Selection

### 2.1 Factory creation

```cpp
UINT factoryFlags = 0;
#if !defined(NDEBUG) || defined(ENGINE_DEVREL)
    factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
ComPtr<IDXGIFactory7> factory;
ENGINE_HR(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)));
```

### 2.2 Adapter enumeration

```cpp
ComPtr<IDXGIAdapter1> adapter;
for (UINT i = 0; factory->EnumAdapterByGpuPreference(
         i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
    // Probe: create device without storing it
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1,
                                    __uuidof(ID3D12Device), nullptr))) {
        // Log adapter name and VRAM
        LOG_INFO("Selected GPU: {} ({} MB VRAM)",
                 narrow(desc.Description),
                 desc.DedicatedVideoMemory / (1024 * 1024));
        break;
    }
}
ENGINE_ASSERT(adapter, "No DX12 Feature Level 12_1 adapter found");
```

`narrow()` is a small helper that converts `std::wstring` to `std::string` via WideCharToMultiByte — lives in `rendering/internal/WinUtil.h`.

### 2.3 Feature-level capability probing

After device creation (§3.1), probe optional features:

```cpp
D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));

D3D12_FEATURE_DATA_SHADER_MODEL sm{ D3D_SHADER_MODEL_6_6 };
if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))))
    sm.HighestShaderModel = D3D_SHADER_MODEL_6_5;
shaderModel_ = sm.HighestShaderModel;

D3D12_FEATURE_DATA_D3D12_OPTIONS5 opt5{};
device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opt5, sizeof(opt5));
dxrSupported_ = (opt5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1);
```

Log all capabilities at `LOG_INFO` level on startup. Store results in `GpuDevice` private members.

---

## 3. Device and Command Queues

### 3.1 Device creation

```cpp
ENGINE_HR(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device_)));
device_->SetName(L"EngineDevice");
```

### 3.2 Three command queues

```cpp
auto makeQueue = [&](D3D12_COMMAND_LIST_TYPE type, const wchar_t* name) {
    D3D12_COMMAND_QUEUE_DESC desc{ .Type = type, .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL };
    ComPtr<ID3D12CommandQueue> q;
    ENGINE_HR(device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&q)));
    q->SetName(name);
    return q;
};
graphicsQueue_ = makeQueue(D3D12_COMMAND_LIST_TYPE_DIRECT,  L"GraphicsQueue");
computeQueue_  = makeQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE,  L"ComputeQueue");
copyQueue_     = makeQueue(D3D12_COMMAND_LIST_TYPE_COPY,     L"CopyQueue");
```

Compute and copy queues are idle in v1 but must exist (Tools Lead's asset cooker may use the copy queue in a future pass).

### 3.3 Per-frame command allocators and lists

```cpp
constexpr uint32_t kBackBufferCount    = 3;
constexpr uint32_t kMaxFramesInFlight  = 2;  // CPU never more than 2 frames ahead of GPU
```

Allocate `kBackBufferCount` command allocators and one reusable command list:

```cpp
for (uint32_t i = 0; i < kBackBufferCount; ++i) {
    ENGINE_HR(device_->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocators_[i])));
    cmdAllocators_[i]->SetName(std::format(L"CmdAllocator[{}]", i).c_str());
}
ENGINE_HR(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
    cmdAllocators_[0].Get(), nullptr, IID_PPV_ARGS(&cmdList_)));
cmdList_->Close();  // begin closed; Reset() at frame start
```

---

## 4. Swapchain

### 4.1 Tearing support check

```cpp
ComPtr<IDXGIFactory5> factory5;
factory_.As(&factory5);
BOOL tearingSupported = FALSE;
if (factory5)
    factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupported, sizeof(tearingSupported));
tearingSupported_ = (tearingSupported == TRUE);
```

### 4.2 Swapchain creation

```cpp
DXGI_SWAP_CHAIN_DESC1 desc{
    .Width       = window_.clientWidth(),
    .Height      = window_.clientHeight(),
    .Format      = DXGI_FORMAT_R8G8B8A8_UNORM,
    .SampleDesc  = {1, 0},
    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
    .BufferCount = kBackBufferCount,
    .SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    .Flags       = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u,
};
ComPtr<IDXGISwapChain1> sc1;
ENGINE_HR(factory_->CreateSwapChainForHwnd(
    graphicsQueue_.Get(),
    static_cast<HWND>(window_.nativeHandle()),
    &desc, nullptr, nullptr, &sc1));
ENGINE_HR(factory_->MakeWindowAssociation(
    static_cast<HWND>(window_.nativeHandle()), DXGI_MWA_NO_ALT_ENTER));
ENGINE_HR(sc1.As(&swapchain_));
```

### 4.3 Back-buffer RTVs

Allocate a CPU-only RTV descriptor heap:

```cpp
D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{
    .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
    .NumDescriptors = kBackBufferCount,
    .Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
};
ENGINE_HR(device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_)));
rtvDescSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
```

For each back buffer:
```cpp
for (uint32_t i = 0; i < kBackBufferCount; ++i) {
    ENGINE_HR(swapchain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])));
    backBuffers_[i]->SetName(std::format(L"BackBuffer[{}]", i).c_str());
    auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtvHeap_->GetCPUDescriptorHandleForHeapStart(), i, rtvDescSize_);
    device_->CreateRenderTargetView(backBuffers_[i].Get(), nullptr, handle);
    rtvHandles_[i] = handle;
}
```

### 4.4 Present

```cpp
const UINT syncInterval = vsync_ ? 1u : 0u;
const UINT flags = (tearingSupported_ && !vsync_) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
ENGINE_HR(swapchain_->Present(syncInterval, flags));
```

`vsync_` defaults to `true`; toggleable at runtime via `[render].vsync` config key.

### 4.5 Swapchain resize

Triggered when `core::events::WindowResized` is posted on the event bus. Processing is deferred to the top of the next frame (not inline in WndProc):

1. `flush()` — wait for all in-flight GPU work.
2. Release all `backBuffers_[i]` COM pointers.
3. `swapchain_->ResizeBuffers(0, newW, newH, DXGI_FORMAT_UNKNOWN, tearingFlags)`.
4. Re-acquire back buffers, recreate RTVs, recreate depth-stencil buffer with new dimensions.
5. Update `width_` / `height_` stored on `GpuDevice`.

---

## 5. Debug Layer and Validation

| Build config | `ID3D12Debug1` | GPU-based validation | `IDXGIInfoQueue` | PIX |
|---|---|---|---|---|
| Debug | Enabled | Enabled | Enabled | No |
| DevRel (`ENGINE_DEVREL`) | Enabled | Enabled | Enabled | Yes |
| Release | None | None | None | No |

Activation (must precede `D3D12CreateDevice`):

```cpp
#if !defined(NDEBUG) || defined(ENGINE_DEVREL)
    ComPtr<ID3D12Debug1> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
        debug->SetEnableGPUBasedValidation(true);
        LOG_INFO("DX12 debug layer enabled (GPU-based validation on)");
    }
#endif
```

After device creation, enable `ID3D12InfoQueue` break-on-severity in Debug/DevRel:
```cpp
ComPtr<ID3D12InfoQueue> iq;
if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&iq)))) {
    iq->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,   TRUE);
    iq->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
}
```

---

## 6. Per-Frame Fence Pattern

One `ID3D12Fence` per back buffer slot plus one CPU `HANDLE` event (shared across frames):

```
frameIndex  = swapchain_->GetCurrentBackBufferIndex()
fenceValue  = monotonically increasing uint64_t, starting at 1

Frame N:
  Wait until fence value (N - kMaxFramesInFlight) is signaled on the GPU
  Reset cmdAllocators_[frameIndex]
  cmdList_->Reset(cmdAllocators_[frameIndex].Get(), nullptr)
  ... record render work ...
  cmdList_->Close()
  ID3D12CommandList* lists[] = { cmdList_.Get() };
  graphicsQueue_->ExecuteCommandLists(1, lists);
  graphicsQueue_->Signal(fence_.Get(), N);
  Present
  frameFenceValues_[frameIndex] = N;
  ++N;
```

`flush()` signals the fence with a high value and waits for it synchronously — used during resize and shutdown.

---

## 7. GpuDevice Public API Summary

```cpp
// src/rendering/public/rendering/GpuDevice.h
class GpuDevice {
public:
    struct Desc { Window* window; bool vsync = true; };
    static GpuDevice create(const Desc&);

    uint32_t          clientWidth()      const noexcept;
    uint32_t          clientHeight()     const noexcept;
    bool              tearingSupported() const noexcept;
    D3D_FEATURE_LEVEL featureLevel()     const noexcept;  // highest supported
    bool              dxrSupported()     const noexcept;

    void beginFrame();   // wait for oldest in-flight frame, reset allocator
    void endFrame();     // close + execute + signal + present
    void flush();        // wait for all GPU work to complete (shutdown / resize)

    // Escape hatches — for frame graph and PIX integration only:
    void* nativeDevice()      const noexcept;  // ID3D12Device*
    void* nativeCommandList() const noexcept;  // ID3D12GraphicsCommandList* for current frame

    D3D12_CPU_DESCRIPTOR_HANDLE currentBackBufferRtv() const noexcept;
    D3D12_CPU_DESCRIPTOR_HANDLE depthBufferDsv()       const noexcept;

    ENGINE_NO_COPY(GpuDevice);
    GpuDevice(GpuDevice&&) noexcept;
    GpuDevice& operator=(GpuDevice&&) noexcept;
    ~GpuDevice();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

`D3D12_CPU_DESCRIPTOR_HANDLE` is a plain struct (just a `SIZE_T`); it is acceptable in this public header since it carries no DX12 include requirements beyond `<d3d12.h>`, which other engine modules never include. If the Team Leader objects, these two methods can return `uint64_t` (the handle value) and callers reinterpret.
