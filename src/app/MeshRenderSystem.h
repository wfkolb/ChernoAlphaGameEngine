#pragma once
#include <core/ecs/World.h>
#include <core/components/Transform.h>
#include <core/components/MeshHandle.h>
// RenderableComponent is a named alias for MeshHandle (id=12) in the rendering
// namespace. The component itself lives in core; the alias is here for callers
// that want a semantically correct name without pulling in the full rendering lib.
#include <rendering/RenderableComponent.h>
#include <rendering/MaterialManager.h>
#include <rendering/MeshManager.h>
#include <rendering/FrameGraph.h>
#include <rendering/GpuDevice.h>
#include <tools/CpuTexture.h>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include <vector>

namespace engine::app {

enum class ViewMode : uint32_t {
    Lit   = 0,
    Unlit = 1,
};

// Maintains a map from entity index to GPU mesh handle loaded by the meshLoadFn delegate.
// Populated at scene activation; cleared on scene unload.
class MeshRenderSystem {
public:
    MeshRenderSystem();
    ~MeshRenderSystem();

    MeshRenderSystem(const MeshRenderSystem&)            = delete;
    MeshRenderSystem& operator=(const MeshRenderSystem&) = delete;

    // Create the opaque-pass pipeline. Must be called once after GpuDevice is ready.
    // materialManager: external owner; must outlive this MeshRenderSystem.
    // backBufferFormat: DXGI_FORMAT as uint32_t (87 = DXGI_FORMAT_R8G8B8A8_UNORM).
    // depthFormat     : DXGI_FORMAT as uint32_t (20 = DXGI_FORMAT_D32_FLOAT).
    void init(rendering::GpuDevice&      device,
              rendering::MaterialManager& materialManager,
              uint32_t backBufferFormat = 87u,
              uint32_t depthFormat     = 20u);

    // Add one opaque pass per loaded entity mesh into fg each render tick.
    // view/proj/viewProj : row-major matrices (16 floats each).
    //   view     : world -> view space
    //   proj     : view  -> clip space (reverse-Z)
    //   viewProj : pre-multiplied world -> clip space (view * proj)
    // backBuffer / depthBuffer : handles imported into fg by the caller beforehand.
    // width / height : render target dimensions for viewport and scissor.
    // frameSlot  : 0 = editor viewport, 1 = PIE window. Each slot has its own
    //              per-frame constant buffer region so two concurrent render paths
    //              in the same command list don't overwrite each other's matrices.
    void tick(core::ecs::World&        world,
              rendering::MeshManager&  meshManager,
              rendering::FrameGraph&   fg,
              const float              viewMat[16],
              const float              projMat[16],
              const float              viewProjMat[16],
              rendering::ResourceHandle backBuffer,
              rendering::ResourceHandle depthBuffer,
              uint32_t                  width,
              uint32_t                  height,
              uint32_t                  frameSlot      = 0,
              const float               cameraWorldPos[3] = nullptr,
              ViewMode                  viewMode       = ViewMode::Lit);

    // Upload a CPU texture to GPU and register an SRV at the next bindless slot.
    // Returns the SRV slot index. Must be called during an open frame.
    uint32_t uploadTexture(rendering::GpuDevice& device, const tools::CpuTexture& tex);

    // Registers a GPU handle for the given entity index. Called from meshLoadFn.
    void registerHandle(uint32_t entityIndex, rendering::MeshHandle gpuHandle);

    // Releases all registered GPU handles. Called from meshUnloadFn.
    void clear();

    // Returns true when no GPU mesh handles are registered (no renderable meshes in scene).
    bool empty() const noexcept { return handles_.empty(); }

    // Returns true if a GPU mesh has already been uploaded for this entity index.
    bool hasHandle(uint32_t entityIndex) const noexcept { return handles_.count(entityIndex) > 0; }

    // Removes the GPU handle for one entity (e.g. when its MeshHandle component is removed
    // or its asset path changes). The GPU buffers remain valid until MeshManager is destroyed.
    void unregisterHandle(uint32_t entityIndex);

private:
    struct Impl;
    std::unordered_map<uint32_t, rendering::MeshHandle> handles_;
    std::unique_ptr<Impl>                               impl_;
};

} // namespace engine::app
