#include "ReadbackHelper.h"

#include <rendering/GpuDevice.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace engine::rendering {

static uint32_t alignedPitch(int w) {
    const uint32_t raw = static_cast<uint32_t>(w) * 4u;
    return (raw + 255u) & ~255u;
}

std::vector<RGBA8> readbackBackBuffer(GpuDevice& device, int x, int y, int w, int h) {
    auto* d3dDevice  = static_cast<ID3D12Device*>(device.nativeDevice());
    auto* cmdList    = static_cast<ID3D12GraphicsCommandList*>(device.nativeCommandList());
    auto* backBuffer = static_cast<ID3D12Resource*>(device.nativeBackBuffer());

    const uint32_t pitch       = alignedPitch(w);
    const uint64_t bufferSize  = static_cast<uint64_t>(pitch) * static_cast<uint64_t>(h);

    // Create readback buffer
    D3D12_HEAP_PROPERTIES heapProps{ D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = bufferSize;
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.SampleDesc       = { 1, 0 };
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> readbackBuf;
    HRESULT hr = d3dDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&readbackBuf));
    if (FAILED(hr)) return {};

    // Transition back buffer: PRESENT → COPY_SOURCE
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = backBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    // Copy texture region to readback buffer
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource                              = readbackBuf.Get();
    dst.Type                                   = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format       = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width        = static_cast<UINT>(w);
    dst.PlacedFootprint.Footprint.Height       = static_cast<UINT>(h);
    dst.PlacedFootprint.Footprint.Depth        = 1;
    dst.PlacedFootprint.Footprint.RowPitch     = pitch;
    dst.PlacedFootprint.Offset                 = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource        = backBuffer;
    src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    D3D12_BOX box{ static_cast<UINT>(x), static_cast<UINT>(y), 0,
                   static_cast<UINT>(x + w), static_cast<UINT>(y + h), 1 };
    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);

    // Transition back: COPY_SOURCE → PRESENT
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    cmdList->ResourceBarrier(1, &barrier);

    // Execute and wait
    device.endFrame();
    device.flush();

    // Map and read pixels
    void* mapped = nullptr;
    D3D12_RANGE range{ 0, bufferSize };
    hr = readbackBuf->Map(0, &range, &mapped);
    if (FAILED(hr)) return {};

    std::vector<RGBA8> pixels(static_cast<size_t>(w) * static_cast<size_t>(h));
    const uint8_t* src8 = static_cast<const uint8_t*>(mapped);
    for (int row = 0; row < h; ++row) {
        const auto* rowPtr = reinterpret_cast<const RGBA8*>(src8 + static_cast<uint64_t>(row) * pitch);
        std::copy(rowPtr, rowPtr + w, pixels.begin() + row * w);
    }

    D3D12_RANGE writeRange{ 0, 0 };
    readbackBuf->Unmap(0, &writeRange);
    return pixels;
}

} // namespace engine::rendering
