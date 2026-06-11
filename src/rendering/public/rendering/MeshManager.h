#pragma once
#include "rendering/Mesh.h"
#include <core/math/Mat.h>
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

    // Must be called once per frame after GpuDevice::beginFrame() so that
    // uploadStatic() records onto the current frame's open command list.
    // GpuDevice rotates command lists per frame; the pointer captured at
    // construction becomes stale after the first frame.
    void syncCommandList(void* cmdList);

    // Upload a static mesh (VertexStatic). Returns a handle.
    // Upload heap is released after the next GpuDevice::flush().
    MeshHandle uploadStatic(
        std::span<const VertexStatic> vertices,
        std::span<const uint32_t>     indices);

    // Upload a skinned mesh (VertexSkinned vertices). Returns a MeshHandle.
    // bindPose: one Mat4 per bone, up to 256 bones. Stored in the mesh record
    // and forwarded to the skinned PSO as a bone-palette structured buffer.
    MeshHandle uploadSkinned(
        std::span<const VertexSkinned>       vertices,
        std::span<const uint32_t>            indices,
        std::span<const core::math::Mat4>    bindPose = {});

    // Returns true when the given handle refers to a skinned mesh.
    // Used by MeshRenderSystem to select the correct PSO.
    bool isSkinned(MeshHandle h) const;

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
