#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

#include "rendering/FrameGraph.h"
#include "core/diag/Assert.h"
#include "FrameGraphImpl.h"

using Microsoft::WRL::ComPtr;

#define ENGINE_HR(hr) \
    do { HRESULT _hr = (hr); ENGINE_ASSERT(SUCCEEDED(_hr), "HRESULT failure"); } while(0)

namespace engine::rendering {

// ---------------------------------------------------------------------------
// PassResources
// ---------------------------------------------------------------------------

void* PassResources::getResource(ResourceHandle h) const {
    ENGINE_ASSERT(impl_, "PassResources: null impl");
    ENGINE_ASSERT(h.isValid() && h.id < impl_->fg->resources.size(), "PassResources: invalid handle");
    return static_cast<void*>(impl_->fg->resources[h.id].resource.Get());
}

uint64_t PassResources::getRtvHandle(ResourceHandle h) const {
    ENGINE_ASSERT(impl_ && h.isValid() && h.id < impl_->fg->resources.size(),
                  "PassResources: invalid handle");
    return impl_->fg->resources[h.id].cachedRtvPtr;
}

uint64_t PassResources::getDsvHandle(ResourceHandle h) const {
    ENGINE_ASSERT(impl_ && h.isValid() && h.id < impl_->fg->resources.size(),
                  "PassResources: invalid handle");
    return impl_->fg->resources[h.id].cachedDsvPtr;
}

uint64_t PassResources::getSrvHandle(ResourceHandle h) const {
    ENGINE_ASSERT(impl_ && h.isValid() && h.id < impl_->fg->resources.size(),
                  "PassResources: invalid handle");
    return impl_->fg->resources[h.id].cachedSrvPtr;
}

uint64_t PassResources::getUavHandle(ResourceHandle h) const {
    ENGINE_ASSERT(impl_ && h.isValid() && h.id < impl_->fg->resources.size(),
                  "PassResources: invalid handle");
    return impl_->fg->resources[h.id].cachedUavPtr;
}

// ---------------------------------------------------------------------------
// PassBuilder
// ---------------------------------------------------------------------------

FrameGraph::PassBuilder::PassBuilder(Impl* impl) noexcept
    : impl_(impl) {}

ResourceHandle FrameGraph::PassBuilder::read(ResourceHandle h, uint32_t requiredState) {
    ENGINE_ASSERT(impl_ && h.isValid(), "PassBuilder::read: invalid handle");
    impl_->pass->barriers.push_back({ h, static_cast<D3D12_RESOURCE_STATES>(requiredState) });
    return h;
}

ResourceHandle FrameGraph::PassBuilder::write(ResourceHandle h, uint32_t requiredState) {
    ENGINE_ASSERT(impl_ && h.isValid(), "PassBuilder::write: invalid handle");
    impl_->pass->barriers.push_back({ h, static_cast<D3D12_RESOURCE_STATES>(requiredState) });
    return h;
}

ResourceHandle FrameGraph::PassBuilder::create(const TextureDesc& desc, std::string_view /*name*/) {
    ENGINE_ASSERT(impl_, "PassBuilder::create: null impl");
    VirtualResource vr;
    vr.desc         = desc;
    vr.isImported   = false;
    vr.currentState = D3D12_RESOURCE_STATE_COMMON;
    impl_->fg->resources.push_back(std::move(vr));
    return ResourceHandle{ static_cast<uint16_t>(impl_->fg->resources.size() - 1) };
}

ResourceHandle FrameGraph::PassBuilder::import(void* resource, uint32_t currentState,
                                               std::string_view /*name*/) {
    ENGINE_ASSERT(impl_ && resource, "PassBuilder::import: null resource");
    VirtualResource vr;
    vr.isImported   = true;
    vr.currentState = static_cast<D3D12_RESOURCE_STATES>(currentState);
    // Attach + AddRef: we borrow the resource; ComPtr::Release on destruction restores balance.
    vr.resource.Attach(static_cast<ID3D12Resource*>(resource));
    vr.resource->AddRef();
    impl_->fg->resources.push_back(std::move(vr));
    return ResourceHandle{ static_cast<uint16_t>(impl_->fg->resources.size() - 1) };
}

// ---------------------------------------------------------------------------
// FrameGraph — construction / move / destruction
// ---------------------------------------------------------------------------

FrameGraph::FrameGraph()
    : impl_(std::make_unique<Impl>()) {}

FrameGraph::FrameGraph(FrameGraph&&) noexcept = default;
FrameGraph& FrameGraph::operator=(FrameGraph&&) noexcept = default;
FrameGraph::~FrameGraph() = default;

// ---------------------------------------------------------------------------
// importBackBuffer / importDepthBuffer
// ---------------------------------------------------------------------------

ResourceHandle FrameGraph::importBackBuffer(void* backBuf, uint64_t rtvHandle,
                                             uint32_t currentState) {
    ENGINE_ASSERT(backBuf, "importBackBuffer: null resource");
    VirtualResource vr;
    vr.isImported   = true;
    vr.isBackBuffer = true;
    vr.currentState = static_cast<D3D12_RESOURCE_STATES>(currentState);
    vr.cachedRtvPtr = rtvHandle;
    vr.resource.Attach(static_cast<ID3D12Resource*>(backBuf));
    vr.resource->AddRef();
    impl_->resources.push_back(std::move(vr));
    return ResourceHandle{ static_cast<uint16_t>(impl_->resources.size() - 1) };
}

ResourceHandle FrameGraph::importDepthBuffer(void* depth, uint64_t dsvHandle) {
    ENGINE_ASSERT(depth, "importDepthBuffer: null resource");
    VirtualResource vr;
    vr.isImported   = true;
    vr.currentState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    vr.cachedDsvPtr = dsvHandle;
    vr.resource.Attach(static_cast<ID3D12Resource*>(depth));
    vr.resource->AddRef();
    impl_->resources.push_back(std::move(vr));
    return ResourceHandle{ static_cast<uint16_t>(impl_->resources.size() - 1) };
}

// ---------------------------------------------------------------------------
// addPass
// ---------------------------------------------------------------------------

void FrameGraph::addPass(std::string_view name,
                          std::function<void(PassBuilder&)> setup,
                          ExecuteFn execute) {
    impl_->passes.emplace_back();
    RegisteredPass& pass = impl_->passes.back();
    pass.name    = std::string(name);
    pass.execute = std::move(execute);

    PassBuilder::Impl builderImpl;
    builderImpl.fg   = impl_.get();
    builderImpl.pass = &pass;

    PassBuilder builder(&builderImpl);
    setup(builder);
}

// ---------------------------------------------------------------------------
// compile
// ---------------------------------------------------------------------------

static bool flagsNeedRtv(uint32_t flags) {
    return (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0;
}

static bool flagsNeedDsv(uint32_t flags) {
    return (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0;
}

void FrameGraph::compile() {
    ENGINE_ASSERT(impl_->device,
                  "FrameGraph::compile: device not set. Call frameGraphSetDevice() first.");

    ID3D12Device* dev = impl_->device;

    impl_->transientRtvs.resize(impl_->resources.size());
    impl_->transientDsvs.resize(impl_->resources.size());

    for (size_t i = 0; i < impl_->resources.size(); ++i) {
        VirtualResource& vr = impl_->resources[i];

        // Allocate committed GPU resource for transient (non-imported) resources.
        if (!vr.isImported && !vr.resource) {
            const bool isDepth = flagsNeedDsv(vr.desc.resourceFlags);
            const bool isRt    = flagsNeedRtv(vr.desc.resourceFlags);

            D3D12_HEAP_PROPERTIES heapProps = {};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC rd = {};
            rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            rd.Width            = vr.desc.width;
            rd.Height           = vr.desc.height;
            rd.DepthOrArraySize = 1;
            rd.MipLevels        = static_cast<UINT16>(vr.desc.mipLevels);
            rd.Format           = static_cast<DXGI_FORMAT>(vr.desc.dxgiFormat);
            rd.SampleDesc       = { 1, 0 };
            rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            rd.Flags            = static_cast<D3D12_RESOURCE_FLAGS>(vr.desc.resourceFlags);

            D3D12_CLEAR_VALUE  clearVal = {};
            D3D12_CLEAR_VALUE* pClear   = nullptr;

            if (isDepth) {
                clearVal.Format               = static_cast<DXGI_FORMAT>(vr.desc.dxgiFormat);
                clearVal.DepthStencil.Depth   = vr.desc.clearDepth;
                clearVal.DepthStencil.Stencil = vr.desc.clearStencil;
                pClear          = &clearVal;
                vr.currentState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            } else if (isRt) {
                clearVal.Format   = static_cast<DXGI_FORMAT>(vr.desc.dxgiFormat);
                clearVal.Color[0] = vr.desc.clearColor[0];
                clearVal.Color[1] = vr.desc.clearColor[1];
                clearVal.Color[2] = vr.desc.clearColor[2];
                clearVal.Color[3] = vr.desc.clearColor[3];
                pClear          = &clearVal;
                vr.currentState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }

            ENGINE_HR(dev->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &rd,
                vr.currentState,
                pClear,
                IID_PPV_ARGS(&vr.resource)));
        }

        // Create a single-descriptor RTV heap for transient render targets.
        if (!vr.isImported && vr.resource && flagsNeedRtv(vr.desc.resourceFlags)
            && vr.cachedRtvPtr == 0) {
            D3D12_DESCRIPTOR_HEAP_DESC hd = {};
            hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            hd.NumDescriptors = 1;
            hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            ENGINE_HR(dev->CreateDescriptorHeap(&hd,
                          IID_PPV_ARGS(&impl_->transientRtvs[i].heap)));
            impl_->transientRtvs[i].cpuHandle =
                impl_->transientRtvs[i].heap->GetCPUDescriptorHandleForHeapStart();
            dev->CreateRenderTargetView(vr.resource.Get(), nullptr,
                                        impl_->transientRtvs[i].cpuHandle);
            vr.cachedRtvPtr = impl_->transientRtvs[i].cpuHandle.ptr;
        }

        // Create a single-descriptor DSV heap for transient depth resources.
        if (!vr.isImported && vr.resource && flagsNeedDsv(vr.desc.resourceFlags)
            && vr.cachedDsvPtr == 0) {
            D3D12_DESCRIPTOR_HEAP_DESC hd = {};
            hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            hd.NumDescriptors = 1;
            hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            ENGINE_HR(dev->CreateDescriptorHeap(&hd,
                          IID_PPV_ARGS(&impl_->transientDsvs[i].heap)));
            impl_->transientDsvs[i].cpuHandle =
                impl_->transientDsvs[i].heap->GetCPUDescriptorHandleForHeapStart();
            dev->CreateDepthStencilView(vr.resource.Get(), nullptr,
                                        impl_->transientDsvs[i].cpuHandle);
            vr.cachedDsvPtr = impl_->transientDsvs[i].cpuHandle.ptr;
        }
    }
}

// ---------------------------------------------------------------------------
// execute
// ---------------------------------------------------------------------------

void FrameGraph::execute(void* cmdListVoid) {
    ENGINE_ASSERT(cmdListVoid, "FrameGraph::execute: null command list");
    auto* cmd = static_cast<ID3D12GraphicsCommandList*>(cmdListVoid);

    // Stack-local impl wired into PassResources for this call.
    PassResources::Impl prImpl;
    prImpl.fg = impl_.get();

    PassResources passResources;
    passResources.impl_ = &prImpl;

    std::vector<D3D12_RESOURCE_BARRIER> barrierBatch;
    barrierBatch.reserve(16);

    for (RegisteredPass& pass : impl_->passes) {
        barrierBatch.clear();

        for (const BarrierReq& req : pass.barriers) {
            VirtualResource& vr = impl_->resources[req.handle.id];
            if (!vr.resource)                    continue;
            if (vr.currentState == req.requiredState) continue;

            D3D12_RESOURCE_BARRIER b = {};
            b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            b.Transition.pResource   = vr.resource.Get();
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = vr.currentState;
            b.Transition.StateAfter  = req.requiredState;
            barrierBatch.push_back(b);

            vr.currentState = req.requiredState;
        }

        if (!barrierBatch.empty()) {
            cmd->ResourceBarrier(static_cast<UINT>(barrierBatch.size()),
                                 barrierBatch.data());
        }

        pass.execute(static_cast<void*>(cmd), passResources);
    }

    // Transition imported resources back to their expected post-frame states.
    // Back buffers are intentionally skipped here: GpuDevice::endFrame() owns the
    // RENDER_TARGET→PRESENT transition for the swap-chain back buffer. Emitting it
    // twice would produce a D3D12 validation error (wrong StateBefore on the second).
    barrierBatch.clear();
    for (VirtualResource& vr : impl_->resources) {
        if (!vr.isImported || !vr.resource) continue;
        if (vr.isBackBuffer) continue;

        D3D12_RESOURCE_STATES targetState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

        if (vr.currentState == targetState) continue;

        D3D12_RESOURCE_BARRIER b = {};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        b.Transition.pResource   = vr.resource.Get();
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = vr.currentState;
        b.Transition.StateAfter  = targetState;
        barrierBatch.push_back(b);
        vr.currentState = targetState;
    }
    if (!barrierBatch.empty()) {
        cmd->ResourceBarrier(static_cast<UINT>(barrierBatch.size()),
                             barrierBatch.data());
    }
}

// ---------------------------------------------------------------------------
// reset
// ---------------------------------------------------------------------------

void FrameGraph::reset() {
    impl_->passes.clear();
    // Release all resources (imported + transient). Imported resources were
    // AddRef'd on import; releasing here restores the refcount.
    impl_->resources.clear();
    impl_->transientRtvs.clear();
    impl_->transientDsvs.clear();
}

// ---------------------------------------------------------------------------
// Free helper
// ---------------------------------------------------------------------------

void setFullscreenViewportScissor(void* cmdListVoid, uint32_t width, uint32_t height) {
    ENGINE_ASSERT(cmdListVoid, "setFullscreenViewportScissor: null command list");
    auto* cmd = static_cast<ID3D12GraphicsCommandList*>(cmdListVoid);

    D3D12_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width    = static_cast<float>(width);
    vp.Height   = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    cmd->RSSetViewports(1, &vp);

    D3D12_RECT scissor = {};
    scissor.left   = 0;
    scissor.top    = 0;
    scissor.right  = static_cast<LONG>(width);
    scissor.bottom = static_cast<LONG>(height);
    cmd->RSSetScissorRects(1, &scissor);
}

// ---------------------------------------------------------------------------
// Internal helpers for the rest of the rendering module
// ---------------------------------------------------------------------------

void frameGraphSetDevice(FrameGraph& fg, void* device) {
    fg.impl_->device = static_cast<ID3D12Device*>(device);
}

} // namespace engine::rendering
