#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <functional>
#include "rendering/FrameGraph.h"

namespace engine::rendering {

// Barrier requirement recorded during setup.
struct BarrierReq {
    ResourceHandle handle;
    D3D12_RESOURCE_STATES requiredState;
};

struct VirtualResource {
    TextureDesc  desc;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
    bool isImported  = false;
    bool isBackBuffer = false;
    uint64_t cachedRtvPtr = 0;   // pre-set for imported back buffer / render targets
    uint64_t cachedDsvPtr = 0;   // pre-set for imported depth buffer / depth targets
    uint64_t cachedSrvPtr = 0;
    uint64_t cachedUavPtr = 0;
};

struct RegisteredPass {
    std::string             name;
    ExecuteFn               execute;
    std::vector<BarrierReq> barriers;  // ordered list of required transitions before this pass
};

// One small RTV heap per transient resource that needs an RTV created on demand.
struct TransientDescriptor {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_CPU_DESCRIPTOR_HANDLE                  cpuHandle{};
};

struct FrameGraph::Impl {
    ID3D12Device*  device  = nullptr;

    std::vector<VirtualResource>    resources;
    std::vector<RegisteredPass>     passes;

    // Transient RTV/DSV heaps keyed by resource index.
    std::vector<TransientDescriptor> transientRtvs;
    std::vector<TransientDescriptor> transientDsvs;
};

// PassBuilder::Impl lives here so FrameGraph.cpp can see it.
struct FrameGraph::PassBuilder::Impl {
    FrameGraph::Impl* fg    = nullptr;
    RegisteredPass*   pass  = nullptr;
};

// PassResources::Impl — passed by pointer into each execute call.
struct PassResources::Impl {
    FrameGraph::Impl* fg = nullptr;
};

// Internal helper: wire a raw ID3D12Device* into the FrameGraph before compile().
// Call immediately after constructing a FrameGraph: frameGraphSetDevice(fg, gpuDevice.nativeDevice()).
void frameGraphSetDevice(FrameGraph& fg, void* device);

} // namespace engine::rendering
