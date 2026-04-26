#pragma once
#include "rendering/Material.h"
#include <memory>

namespace engine::rendering {

class GpuDevice;

// Manages a GPU-side structured buffer of GpuMaterial entries.
// Materials are uploaded once, indexed by MaterialHandle.
class MaterialManager {
public:
    static constexpr uint32_t kMaxMaterials = 4096;

    explicit MaterialManager(GpuDevice& device);
    ~MaterialManager();

    MaterialManager(const MaterialManager&) = delete;
    MaterialManager& operator=(const MaterialManager&) = delete;

    // Add a material; returns handle.
    MaterialHandle add(const GpuMaterial& mat);

    // GPU virtual address of the material structured buffer (for binding as SRV).
    uint64_t gpuVirtualAddress() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::rendering
