#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <rendering/MeshManager.h>

namespace engine::rendering {

struct MeshEntry {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    D3D12_INDEX_BUFFER_VIEW  ibv;
    uint32_t indexCount;
};

struct MeshManager::Impl {
    ID3D12Device*              device  = nullptr;
    ID3D12GraphicsCommandList* cmdList = nullptr;
    std::vector<MeshEntry>     meshes;
    // Upload heaps kept alive until flush
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> pendingUploads;
};

} // namespace engine::rendering
