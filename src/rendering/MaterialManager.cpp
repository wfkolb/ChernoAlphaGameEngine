#include <rendering/MaterialManager.h>
#include <rendering/GpuDevice.h>
#include <core/diag/Assert.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstring>
#include <string>
#include <vector>

namespace engine::rendering {

struct MaterialManager::Impl {
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    GpuMaterial*                           mapped = nullptr;
    uint32_t                               count  = 0;
    std::vector<std::string>               names;  // parallel to mapped[], one entry per material
};

MaterialManager::MaterialManager(GpuDevice& device)
    : impl_(std::make_unique<Impl>())
{
    auto* d3dDevice = static_cast<ID3D12Device*>(device.nativeDevice());
    ENGINE_ASSERT(d3dDevice, "nativeDevice returned null");

    const UINT64 bufferSize = static_cast<UINT64>(kMaxMaterials) * sizeof(GpuMaterial);

    D3D12_HEAP_PROPERTIES uploadHeapProps{};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = bufferSize;
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = d3dDevice->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&impl_->buffer));
    ENGINE_VERIFY(SUCCEEDED(hr), "Failed to create material buffer");

    // Permanently mapped; write from CPU side directly.
    D3D12_RANGE readRange{ 0, 0 };
    hr = impl_->buffer->Map(0, &readRange, reinterpret_cast<void**>(&impl_->mapped));
    ENGINE_VERIFY(SUCCEEDED(hr), "Failed to map material buffer");
}

MaterialManager::~MaterialManager()
{
    if (impl_ && impl_->buffer && impl_->mapped) {
        impl_->buffer->Unmap(0, nullptr);
        impl_->mapped = nullptr;
    }
}

MaterialHandle MaterialManager::add(const GpuMaterial& mat)
{
    return add(mat, "");
}

MaterialHandle MaterialManager::add(const GpuMaterial& mat, std::string_view name)
{
    ENGINE_ASSERT(impl_->count < kMaxMaterials, "MaterialManager: exceeded kMaxMaterials");

    const uint32_t idx = impl_->count++;
    std::memcpy(&impl_->mapped[idx], &mat, sizeof(GpuMaterial));
    impl_->names.emplace_back(name);

    return MaterialHandle{ static_cast<uint16_t>(idx) };
}

const char* MaterialManager::getName(MaterialHandle handle) const
{
    if (!handle.isValid() || handle.index >= impl_->names.size()) return "";
    return impl_->names[handle.index].c_str();
}

GpuMaterial* MaterialManager::getMutable(MaterialHandle handle)
{
    if (!handle.isValid() || handle.index >= impl_->count) return nullptr;
    return &impl_->mapped[handle.index];
}

uint32_t MaterialManager::count() const
{
    return impl_->count;
}

uint64_t MaterialManager::gpuVirtualAddress() const
{
    return impl_->buffer->GetGPUVirtualAddress();
}

} // namespace engine::rendering
