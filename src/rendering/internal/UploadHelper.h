#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstddef>

namespace engine::rendering::internal {

// Upload data to a GPU default-heap resource via a temporary upload heap.
// The upload heap buffer is returned; the caller must keep it alive until
// the command list has been executed on the GPU (i.e., until the next flush).
Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer(
    ID3D12Device*              device,
    ID3D12GraphicsCommandList* cmdList,
    const void*                data,
    size_t                     byteSize,
    D3D12_RESOURCE_STATES      finalState,
    Microsoft::WRL::ComPtr<ID3D12Resource>& outDefaultHeapResource);

} // namespace engine::rendering::internal
