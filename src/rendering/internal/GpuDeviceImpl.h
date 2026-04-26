#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <rendering/GpuDevice.h>
using Microsoft::WRL::ComPtr;

namespace engine::rendering {

struct PerFrameResources {
    ComPtr<ID3D12CommandAllocator>    allocator;
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    uint64_t                          fenceValue = 0;
};

struct GpuDevice::Impl {
    ComPtr<IDXGIFactory7>         factory;
    ComPtr<IDXGIAdapter4>         adapter;
    ComPtr<ID3D12Device5>         device;

    ComPtr<ID3D12CommandQueue>    directQueue;
    ComPtr<ID3D12CommandQueue>    computeQueue;
    ComPtr<ID3D12CommandQueue>    copyQueue;

    ComPtr<IDXGISwapChain4>       swapChain;
    ComPtr<ID3D12Resource>        backBuffers[GpuDevice::kBackBufferCount];

    ComPtr<ID3D12DescriptorHeap>  rtvHeap;
    ComPtr<ID3D12DescriptorHeap>  dsvHeap;
    ComPtr<ID3D12DescriptorHeap>  srvHeap;
    ComPtr<ID3D12DescriptorHeap>  samplerHeap;

    ComPtr<ID3D12Resource>        depthBuffer;

    PerFrameResources             frames[GpuDevice::kMaxFramesInFlight];
    uint32_t                      frameIndex     = 0;

    uint32_t                      backBufferIndex  = 0;
    uint32_t                      backBufferWidth  = 0;
    uint32_t                      backBufferHeight = 0;
    DXGI_FORMAT                   backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    ComPtr<ID3D12Fence>           fence;
    HANDLE                        fenceEvent   = nullptr;
    uint64_t                      fenceCounter = 0;

    uint32_t                      rtvDescSize     = 0;
    uint32_t                      dsvDescSize     = 0;
    uint32_t                      srvDescSize     = 0;
    uint32_t                      samplerDescSize = 0;

    uint32_t                      featureLevel = 0;

    bool                          valid            = true;
    bool                          vsync            = true;
    bool                          tearingSupported = false;
    bool                          dxrSupported     = false;

    ComPtr<ID3D12Debug1>          debugController;
};

} // namespace engine::rendering
