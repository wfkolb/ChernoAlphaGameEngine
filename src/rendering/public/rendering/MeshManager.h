#pragma once
#include "rendering/Mesh.h"
#include <span>
#include <cstdint>
#include <memory>

namespace engine::rendering {

class GpuDevice;

// Manages GPU mesh data. Uploads vertex and index buffers to the default heap.
// MeshHandles remain valid until the MeshManager is destroyed.
class MeshManager {
public:
    explicit MeshManager(GpuDevice& device);
    ~MeshManager();

    MeshManager(const MeshManager&) = delete;
    MeshManager& operator=(const MeshManager&) = delete;

    // Upload a static mesh (VertexStatic). Returns a handle.
    // Upload heap is released after the next GpuDevice::flush().
    MeshHandle uploadStatic(
        std::span<const VertexStatic> vertices,
        std::span<const uint32_t>     indices);

    // Get D3D12_VERTEX_BUFFER_VIEW for a mesh (as void* to avoid DX12 in public header).
    // Cast result to D3D12_VERTEX_BUFFER_VIEW* in internal code.
    const void* vertexBufferView(MeshHandle h) const;
    const void* indexBufferView (MeshHandle h) const;
    uint32_t    indexCount       (MeshHandle h) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::rendering
