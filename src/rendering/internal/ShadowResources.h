#pragma once
#include <cstdint>

// Internal header — DX12 types are hidden behind void* to keep them out of
// the public GpuDevice API.  Only rendering/*.cpp translation units include this.

namespace engine::rendering {

// R2 shadow map constants (also exposed in ShadowPass.h for the C++ cascade math).
// Duplicated here to avoid a dependency on the public ShadowPass.h from internals.
inline constexpr uint32_t kShadowCascadeCountInternal    = 4;
inline constexpr uint32_t kShadowMapResolutionInternal   = 2048;
inline constexpr uint32_t kShadowMapSpotResolution       = 512;
inline constexpr uint32_t kMaxShadowCastingSpotsInternal = 8;

// Owns the GPU resources for shadow depth maps.  All DX12 handles are stored
// as void*/uint64_t so this header is safe to include from non-DX12 .cpp files.
struct ShadowResources {
    // ID3D12Resource* for the two depth arrays (nullptr when not allocated).
    void* cascadeTexArray { nullptr };  // DXGI_FORMAT_D32_FLOAT, 2048x2048, 4 slices
    void* spotTexArray    { nullptr };  // DXGI_FORMAT_D32_FLOAT,  512x512, up to 8 slices

    // D3D12_CPU_DESCRIPTOR_HANDLE.ptr for one DSV per cascade slice (0..3).
    uint64_t cascadeDsvHandles[kShadowCascadeCountInternal]    = {};
    // D3D12_CPU_DESCRIPTOR_HANDLE.ptr for one DSV per spot slice (0..7).
    uint64_t spotDsvHandles[kMaxShadowCastingSpotsInternal]    = {};

    // D3D12_CPU_DESCRIPTOR_HANDLE.ptr for the SRV covering the full cascade array.
    // Bind to descriptor heap slot gShadowCascades (t5, space0) during the opaque pass.
    uint64_t cascadeSrvHandle { 0 };
    // D3D12_CPU_DESCRIPTOR_HANDLE.ptr for the SRV covering the full spot array (t6, space0).
    uint64_t spotSrvHandle    { 0 };

    bool valid { false };

    // Release the underlying D3D12 resources.  Safe to call even when !valid.
    void release() noexcept;
};

// Allocate shadow depth-buffer texture arrays and create DSV/SRV descriptors.
//
// d3dDevice     : ID3D12Device* cast to void*
// dsvHeap       : ID3D12DescriptorHeap* (DSV heap) cast to void*
// cbvSrvUavHeap : ID3D12DescriptorHeap* (CBV/SRV/UAV heap) cast to void*
// nextDsvSlot   : in/out — next available slot index in the DSV heap
// nextHeapSlot  : in/out — next available slot index in the CBV/SRV/UAV heap
//
// Returns a ShadowResources with valid == true on success, valid == false on failure.
//
// TODO Phase 10 R2: wire into GpuDevice::create() / the FrameGraph build step.
ShadowResources createShadowResources(
    void*     d3dDevice,
    void*     dsvHeap,
    void*     cbvSrvUavHeap,
    uint32_t& nextDsvSlot,
    uint32_t& nextHeapSlot) noexcept;

} // namespace engine::rendering
