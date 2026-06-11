#pragma once
#include "rendering/Material.h"
#include <memory>
#include <string_view>

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

    // Add a material with a display name; returns handle.
    MaterialHandle add(const GpuMaterial& mat, std::string_view name);

    // Returns the display name for a handle. Empty string if not found or unnamed.
    const char* getName(MaterialHandle handle) const;

    // Returns a mutable pointer to the mapped material at the given handle's index,
    // or nullptr if the handle is invalid. Writes are GPU-visible on the next draw.
    GpuMaterial* getMutable(MaterialHandle handle);

    // Number of materials currently loaded.
    uint32_t count() const;

    // GPU virtual address of the material structured buffer (for binding as SRV).
    uint64_t gpuVirtualAddress() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::rendering
