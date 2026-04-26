#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace engine::rendering {

// Root signature and PSO for the opaque PBR pass.
struct OpaquePassPipeline {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
};

// Create the root signature + PSO for the opaque PBR pass.
// vsBlob / psBlob : compiled shader bytecode (ID3DBlob* cast to void*).
// backBufferFormat: DXGI_FORMAT as uint32_t (87 = DXGI_FORMAT_R8G8B8A8_UNORM).
// depthFormat     : DXGI_FORMAT as uint32_t (20 = DXGI_FORMAT_D32_FLOAT).
OpaquePassPipeline createOpaquePassPipeline(
    ID3D12Device* device,
    void*         vsBlob,
    void*         psBlob,
    uint32_t      backBufferFormat,
    uint32_t      depthFormat);

} // namespace engine::rendering
