#include "UploadHelper.h"
#include <core/diag/Assert.h>
#include <cstring>

namespace engine::rendering::internal {

Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer(
    ID3D12Device*              device,
    ID3D12GraphicsCommandList* cmdList,
    const void*                data,
    size_t                     byteSize,
    D3D12_RESOURCE_STATES      finalState,
    Microsoft::WRL::ComPtr<ID3D12Resource>& outDefaultHeapResource)
{
    ENGINE_ASSERT(device,  "device must not be null");
    ENGINE_ASSERT(cmdList, "cmdList must not be null");
    ENGINE_ASSERT(data,    "data must not be null");
    ENGINE_ASSERT(byteSize > 0, "byteSize must be > 0");

    D3D12_HEAP_PROPERTIES defaultHeapProps{};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_HEAP_PROPERTIES uploadHeapProps{};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = static_cast<UINT64>(byteSize);
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    // 1. Create DEFAULT heap resource (destination).
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&outDefaultHeapResource));
    ENGINE_VERIFY(SUCCEEDED(hr), "Failed to create default-heap resource");

    // 2. Create UPLOAD heap resource (staging).
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadHeap;
    hr = device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadHeap));
    ENGINE_VERIFY(SUCCEEDED(hr), "Failed to create upload-heap resource");

    // 3. Map, memcpy, unmap.
    void* mapped = nullptr;
    D3D12_RANGE readRange{ 0, 0 }; // no GPU read back to CPU
    hr = uploadHeap->Map(0, &readRange, &mapped);
    ENGINE_VERIFY(SUCCEEDED(hr), "Failed to map upload heap");
    std::memcpy(mapped, data, byteSize);
    uploadHeap->Unmap(0, nullptr);

    // 4. Copy from upload to default heap.
    cmdList->CopyBufferRegion(outDefaultHeapResource.Get(), 0,
                               uploadHeap.Get(), 0,
                               static_cast<UINT64>(byteSize));

    // 5. Barrier: COPY_DEST -> finalState.
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = outDefaultHeapResource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = finalState;
    cmdList->ResourceBarrier(1, &barrier);

    // 6. Return upload heap (caller keeps alive until GPU is done).
    return uploadHeap;
}

} // namespace engine::rendering::internal
