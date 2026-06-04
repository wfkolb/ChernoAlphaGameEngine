#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <rendering/GpuDevice.h>
#include <rendering/Window.h>
#include <core/diag/Assert.h>

#include "GpuDeviceImpl.h"

using Microsoft::WRL::ComPtr;

#define ENGINE_HR(hr) \
    do { HRESULT _hr = (hr); ENGINE_ASSERT(SUCCEEDED(_hr), "HRESULT failure"); } while(0)

namespace engine::rendering {

GpuDevice::GpuDevice(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

GpuDevice::GpuDevice(GpuDevice&&) noexcept = default;
GpuDevice& GpuDevice::operator=(GpuDevice&&) noexcept = default;

GpuDevice::~GpuDevice() {
    if (!impl_) return;
    if (impl_->valid) flush();
    if (impl_->fenceEvent) {
        CloseHandle(impl_->fenceEvent);
        impl_->fenceEvent = nullptr;
    }
    impl_.reset();
}

bool GpuDevice::isValid() const noexcept {
    return impl_ && impl_->valid;
}

bool GpuDevice::isAvailable() {
    ComPtr<IDXGIFactory7> fac;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&fac)))) return false;
    ComPtr<IDXGIAdapter4> adapter;
    if (FAILED(fac->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)))) return false;
    return SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device5), nullptr));
}

GpuDevice GpuDevice::create(const Desc& desc) {
    auto impl = std::make_unique<Impl>();

    impl->vsync  = desc.vsync;
    impl->window = desc.window;

#if !defined(NDEBUG)
    {
        ComPtr<ID3D12Debug> debug0;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug0)))) {
            debug0->EnableDebugLayer();
            ComPtr<ID3D12Debug1> debug1;
            if (SUCCEEDED(debug0.As(&debug1))) {
                debug1->SetEnableGPUBasedValidation(TRUE);
                impl->debugController = debug1;
            }
        }
    }
    UINT factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#else
    UINT factoryFlags = 0;
#endif

    ENGINE_HR(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&impl->factory)));

    ENGINE_HR(impl->factory->EnumAdapterByGpuPreference(
        0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&impl->adapter)));

    ENGINE_HR(D3D12CreateDevice(
        impl->adapter.Get(),
        D3D_FEATURE_LEVEL_12_1,
        IID_PPV_ARGS(&impl->device)));

    impl->featureLevel = 0xC100;

    {
        BOOL tearingSupport = FALSE;
        if (SUCCEEDED(impl->factory->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupport, sizeof(tearingSupport)))) {
            impl->tearingSupported = (tearingSupport == TRUE);
        }
    }

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5 = {};
        if (SUCCEEDED(impl->device->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5)))) {
            impl->dxrSupported = (opts5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0);
        }
    }

    auto makeQueue = [&](D3D12_COMMAND_LIST_TYPE type, ComPtr<ID3D12CommandQueue>& out) {
        D3D12_COMMAND_QUEUE_DESC qd = {};
        qd.Type     = type;
        qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        qd.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
        qd.NodeMask = 0;
        ENGINE_HR(impl->device->CreateCommandQueue(&qd, IID_PPV_ARGS(&out)));
    };

    makeQueue(D3D12_COMMAND_LIST_TYPE_DIRECT,  impl->directQueue);
    makeQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE, impl->computeQueue);
    makeQueue(D3D12_COMMAND_LIST_TYPE_COPY,    impl->copyQueue);

    HWND hwnd = static_cast<HWND>(desc.window->nativeHandle());

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width       = desc.window->clientWidth();
    scd.Height      = desc.window->clientHeight();
    scd.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.Stereo      = FALSE;
    scd.SampleDesc  = { 1, 0 };
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = kBackBufferCount;
    scd.Scaling     = DXGI_SCALING_STRETCH;
    scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;
    scd.Flags       = impl->tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> sc1;
    {
        HRESULT sc_hr = impl->factory->CreateSwapChainForHwnd(
            impl->directQueue.Get(), hwnd, &scd, nullptr, nullptr, &sc1);
        if (FAILED(sc_hr)) {
            impl->valid = false;
            return GpuDevice(std::move(impl));
        }
    }

    ENGINE_HR(impl->factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

    ENGINE_HR(sc1.As(&impl->swapChain));

    impl->backBufferWidth  = scd.Width;
    impl->backBufferHeight = scd.Height;

    auto makeHeap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type,
                        UINT count,
                        D3D12_DESCRIPTOR_HEAP_FLAGS flags,
                        ComPtr<ID3D12DescriptorHeap>& out) {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type           = type;
        hd.NumDescriptors = count;
        hd.Flags          = flags;
        hd.NodeMask       = 0;
        ENGINE_HR(impl->device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&out)));
    };

    makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
             kBackBufferCount,
             D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
             impl->rtvHeap);

    makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
             1,
             D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
             impl->dsvHeap);

    makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
             4096,
             D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
             impl->srvHeap);

    makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
             64,
             D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
             impl->samplerHeap);

    impl->rtvDescSize     = impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    impl->dsvDescSize     = impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    impl->srvDescSize     = impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    impl->samplerDescSize = impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = impl->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < kBackBufferCount; ++i) {
        ENGINE_HR(impl->swapChain->GetBuffer(i, IID_PPV_ARGS(&impl->backBuffers[i])));
        impl->device->CreateRenderTargetView(impl->backBuffers[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += impl->rtvDescSize;
    }

    {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC depthDesc = {};
        depthDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width              = scd.Width;
        depthDesc.Height             = scd.Height;
        depthDesc.DepthOrArraySize   = 1;
        depthDesc.MipLevels          = 1;
        depthDesc.Format             = DXGI_FORMAT_D32_FLOAT;
        depthDesc.SampleDesc         = { 1, 0 };
        depthDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthDesc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearVal   = {};
        clearVal.Format              = DXGI_FORMAT_D32_FLOAT;
        clearVal.DepthStencil.Depth  = 1.0f;
        clearVal.DepthStencil.Stencil = 0;

        ENGINE_HR(impl->device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearVal,
            IID_PPV_ARGS(&impl->depthBuffer)));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags         = D3D12_DSV_FLAG_NONE;

        impl->device->CreateDepthStencilView(
            impl->depthBuffer.Get(),
            &dsvDesc,
            impl->dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        ENGINE_HR(impl->device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&impl->frames[i].allocator)));

        ENGINE_HR(impl->device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            impl->frames[i].allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&impl->frames[i].cmdList)));

        ENGINE_HR(impl->frames[i].cmdList->Close());
    }

    ENGINE_HR(impl->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&impl->fence)));

    impl->fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ENGINE_ASSERT(impl->fenceEvent != nullptr, "Failed to create fence event");

    impl->backBufferIndex = impl->swapChain->GetCurrentBackBufferIndex();

    return GpuDevice(std::move(impl));
}

void GpuDevice::beginFrame() {
    if (!impl_->valid) return;

    // Resize swapchain if the OS window dimensions have changed.
    if (impl_->window) {
        const uint32_t nw = impl_->window->clientWidth();
        const uint32_t nh = impl_->window->clientHeight();
        if (nw > 0 && nh > 0 &&
            (nw != impl_->backBufferWidth || nh != impl_->backBufferHeight)) {
            // Drain GPU before releasing resources.
            ++impl_->fenceCounter;
            impl_->directQueue->Signal(impl_->fence.Get(), impl_->fenceCounter);
            if (impl_->fence->GetCompletedValue() < impl_->fenceCounter) {
                impl_->fence->SetEventOnCompletion(impl_->fenceCounter, impl_->fenceEvent);
                WaitForSingleObjectEx(impl_->fenceEvent, INFINITE, FALSE);
            }

            for (uint32_t i = 0; i < kBackBufferCount; ++i)
                impl_->backBuffers[i].Reset();
            impl_->depthBuffer.Reset();

            UINT scFlags = impl_->tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
            ENGINE_HR(impl_->swapChain->ResizeBuffers(0, nw, nh, DXGI_FORMAT_UNKNOWN, scFlags));

            impl_->backBufferWidth  = nw;
            impl_->backBufferHeight = nh;

            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = impl_->rtvHeap->GetCPUDescriptorHandleForHeapStart();
            for (uint32_t i = 0; i < kBackBufferCount; ++i) {
                ENGINE_HR(impl_->swapChain->GetBuffer(i, IID_PPV_ARGS(&impl_->backBuffers[i])));
                impl_->device->CreateRenderTargetView(impl_->backBuffers[i].Get(), nullptr, rtvHandle);
                rtvHandle.ptr += impl_->rtvDescSize;
            }

            D3D12_HEAP_PROPERTIES hp  = {};
            hp.Type                   = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC dd    = {};
            dd.Dimension              = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            dd.Width                  = nw;
            dd.Height                 = nh;
            dd.DepthOrArraySize       = 1;
            dd.MipLevels              = 1;
            dd.Format                 = DXGI_FORMAT_D32_FLOAT;
            dd.SampleDesc             = { 1, 0 };
            dd.Layout                 = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            dd.Flags                  = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            D3D12_CLEAR_VALUE cv      = {};
            cv.Format                 = DXGI_FORMAT_D32_FLOAT;
            cv.DepthStencil.Depth     = 1.0f;
            ENGINE_HR(impl_->device->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &dd,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
                IID_PPV_ARGS(&impl_->depthBuffer)));

            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            impl_->device->CreateDepthStencilView(
                impl_->depthBuffer.Get(), &dsvDesc,
                impl_->dsvHeap->GetCPUDescriptorHandleForHeapStart());

            impl_->backBufferIndex = impl_->swapChain->GetCurrentBackBufferIndex();
        }
    }

    impl_->frameIndex = (impl_->frameIndex + 1) % kMaxFramesInFlight;

    PerFrameResources& frame = impl_->frames[impl_->frameIndex];

    if (impl_->fence->GetCompletedValue() < frame.fenceValue) {
        ENGINE_HR(impl_->fence->SetEventOnCompletion(frame.fenceValue, impl_->fenceEvent));
        WaitForSingleObjectEx(impl_->fenceEvent, INFINITE, FALSE);
    }

    ENGINE_HR(frame.allocator->Reset());
    ENGINE_HR(frame.cmdList->Reset(frame.allocator.Get(), nullptr));

    impl_->backBufferIndex = impl_->swapChain->GetCurrentBackBufferIndex();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = impl_->backBuffers[impl_->backBufferIndex].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;

    frame.cmdList->ResourceBarrier(1, &barrier);
}

void GpuDevice::endFrame() {
    if (!impl_->valid) return;
    PerFrameResources& frame = impl_->frames[impl_->frameIndex];

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = impl_->backBuffers[impl_->backBufferIndex].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;

    frame.cmdList->ResourceBarrier(1, &barrier);

    ENGINE_HR(frame.cmdList->Close());

    ID3D12CommandList* lists[] = { frame.cmdList.Get() };
    impl_->directQueue->ExecuteCommandLists(1, lists);

    UINT presentFlags = (!impl_->vsync && impl_->tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    ENGINE_HR(impl_->swapChain->Present(impl_->vsync ? 1 : 0, presentFlags));

    ++impl_->fenceCounter;
    ENGINE_HR(impl_->directQueue->Signal(impl_->fence.Get(), impl_->fenceCounter));
    frame.fenceValue = impl_->fenceCounter;
}

void GpuDevice::flush() {
    if (!impl_ || !impl_->valid) return;
    ++impl_->fenceCounter;
    ENGINE_HR(impl_->directQueue->Signal(impl_->fence.Get(), impl_->fenceCounter));

    if (impl_->fence->GetCompletedValue() < impl_->fenceCounter) {
        ENGINE_HR(impl_->fence->SetEventOnCompletion(impl_->fenceCounter, impl_->fenceEvent));
        WaitForSingleObjectEx(impl_->fenceEvent, INFINITE, FALSE);
    }
}

uint32_t GpuDevice::clientWidth() const noexcept {
    return impl_->backBufferWidth;
}

uint32_t GpuDevice::clientHeight() const noexcept {
    return impl_->backBufferHeight;
}

bool GpuDevice::tearingSupported() const noexcept {
    return impl_->tearingSupported;
}

uint32_t GpuDevice::featureLevel() const noexcept {
    return impl_->featureLevel;
}

bool GpuDevice::dxrSupported() const noexcept {
    return impl_->dxrSupported;
}

void* GpuDevice::nativeDevice() const noexcept {
    return static_cast<void*>(impl_->device.Get());
}

void* GpuDevice::nativeCommandList() const noexcept {
    return static_cast<void*>(impl_->frames[impl_->frameIndex].cmdList.Get());
}

void* GpuDevice::nativeCommandQueue() const noexcept {
    return static_cast<void*>(impl_->directQueue.Get());
}

uint64_t GpuDevice::currentBackBufferRtvHandle() const noexcept {
    D3D12_CPU_DESCRIPTOR_HANDLE base = impl_->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    base.ptr += impl_->backBufferIndex * impl_->rtvDescSize;
    return base.ptr;
}

uint64_t GpuDevice::depthBufferDsvHandle() const noexcept {
    return impl_->dsvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
}

uint32_t GpuDevice::currentFrameIndex() const noexcept {
    return impl_->backBufferIndex;
}

void* GpuDevice::nativeBackBuffer() const noexcept {
    return impl_->backBuffers[impl_->backBufferIndex].Get();
}

void* GpuDevice::nativeDepthBuffer() const noexcept {
    if (!impl_ || !impl_->valid) return nullptr;
    return static_cast<void*>(impl_->depthBuffer.Get());
}

} // namespace engine::rendering
