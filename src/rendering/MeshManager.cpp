#include <rendering/MeshManager.h>
#include "internal/MeshManagerImpl.h"
#include "internal/UploadHelper.h"
#include <rendering/GpuDevice.h>
#include <core/diag/Assert.h>

namespace engine::rendering {

MeshManager::MeshManager(GpuDevice& device)
    : impl_(std::make_unique<Impl>())
{
    impl_->device  = static_cast<ID3D12Device*>(device.nativeDevice());
    impl_->cmdList = static_cast<ID3D12GraphicsCommandList*>(device.nativeCommandList());
    ENGINE_ASSERT(impl_->device,  "nativeDevice returned null");
    ENGINE_ASSERT(impl_->cmdList, "nativeCommandList returned null");
}

MeshManager::~MeshManager() = default;

MeshHandle MeshManager::uploadStatic(
    std::span<const VertexStatic> vertices,
    std::span<const uint32_t>     indices)
{
    ENGINE_ASSERT(!vertices.empty(), "vertices must not be empty");
    ENGINE_ASSERT(!indices.empty(),  "indices must not be empty");

    MeshEntry entry{};

    const size_t vbByteSize = vertices.size() * sizeof(VertexStatic);
    const size_t ibByteSize = indices.size()  * sizeof(uint32_t);

    // Upload vertex buffer.
    auto vbUpload = internal::uploadBuffer(
        impl_->device,
        impl_->cmdList,
        vertices.data(),
        vbByteSize,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        entry.vertexBuffer);

    // Upload index buffer.
    auto ibUpload = internal::uploadBuffer(
        impl_->device,
        impl_->cmdList,
        indices.data(),
        ibByteSize,
        D3D12_RESOURCE_STATE_INDEX_BUFFER,
        entry.indexBuffer);

    // Fill vertex buffer view.
    entry.vbv.BufferLocation = entry.vertexBuffer->GetGPUVirtualAddress();
    entry.vbv.SizeInBytes    = static_cast<UINT>(vbByteSize);
    entry.vbv.StrideInBytes  = static_cast<UINT>(sizeof(VertexStatic));

    // Fill index buffer view.
    entry.ibv.BufferLocation = entry.indexBuffer->GetGPUVirtualAddress();
    entry.ibv.SizeInBytes    = static_cast<UINT>(ibByteSize);
    entry.ibv.Format         = DXGI_FORMAT_R32_UINT;

    entry.indexCount = static_cast<uint32_t>(indices.size());

    impl_->meshes.push_back(std::move(entry));
    impl_->pendingUploads.push_back(std::move(vbUpload));
    impl_->pendingUploads.push_back(std::move(ibUpload));

    return MeshHandle{ static_cast<uint32_t>(impl_->meshes.size() - 1) };
}

const void* MeshManager::vertexBufferView(MeshHandle h) const
{
    ENGINE_ASSERT(h.isValid() && static_cast<size_t>(h.id) < impl_->meshes.size(), "invalid MeshHandle");
    return &impl_->meshes[h.id].vbv;
}

const void* MeshManager::indexBufferView(MeshHandle h) const
{
    ENGINE_ASSERT(h.isValid() && static_cast<size_t>(h.id) < impl_->meshes.size(), "invalid MeshHandle");
    return &impl_->meshes[h.id].ibv;
}

uint32_t MeshManager::indexCount(MeshHandle h) const
{
    ENGINE_ASSERT(h.isValid() && static_cast<size_t>(h.id) < impl_->meshes.size(), "invalid MeshHandle");
    return impl_->meshes[h.id].indexCount;
}

} // namespace engine::rendering
