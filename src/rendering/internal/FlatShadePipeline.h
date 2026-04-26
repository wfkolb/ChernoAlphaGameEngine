#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace engine::rendering {

struct FlatShadePipeline {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
};

// Create the root signature and PSO.
// device          : ID3D12Device*
// backBufferFormat: DXGI_FORMAT as uint32_t (87 = DXGI_FORMAT_R8G8B8A8_UNORM)
// depthFormat     : DXGI_FORMAT as uint32_t (20 = DXGI_FORMAT_D32_FLOAT)
FlatShadePipeline createFlatShadePipeline(
    ID3D12Device* device,
    uint32_t      backBufferFormat,
    uint32_t      depthFormat);

// Set root signature, PSO, VB, IB, primitive topology, and root constants on cmdList.
// Call immediately before DrawIndexedInstanced.
// mvp: pointer to 16 floats, row-major World*View*Proj.
void bindFlatShade(
    const FlatShadePipeline&   pipeline,
    ID3D12GraphicsCommandList* cmdList,
    const void*                vbView,   // D3D12_VERTEX_BUFFER_VIEW*
    const void*                ibView,   // D3D12_INDEX_BUFFER_VIEW*
    const float                mvp[16]);

} // namespace engine::rendering
