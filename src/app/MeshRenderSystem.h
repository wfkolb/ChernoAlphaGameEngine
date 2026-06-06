#pragma once
#include <core/ecs/World.h>
#include <core/components/Transform.h>
#include <core/components/MeshHandle.h>
#include <rendering/MeshManager.h>
#include <rendering/FrameGraph.h>
#include <rendering/GpuDevice.h>
#include <unordered_map>
#include <cstdint>
#include <memory>

namespace engine::rendering { struct FlatShadePipeline; }

namespace engine::app {

// Maintains a map from entity index to GPU mesh handle loaded by the meshLoadFn delegate.
// Populated at scene activation; cleared on scene unload.
class MeshRenderSystem {
public:
    MeshRenderSystem();
    ~MeshRenderSystem();

    MeshRenderSystem(const MeshRenderSystem&)            = delete;
    MeshRenderSystem& operator=(const MeshRenderSystem&) = delete;

    // Create the flat-shade pipeline. Must be called once after GpuDevice is ready.
    // backBufferFormat: DXGI_FORMAT as uint32_t (87 = DXGI_FORMAT_R8G8B8A8_UNORM).
    // depthFormat     : DXGI_FORMAT as uint32_t (20 = DXGI_FORMAT_D32_FLOAT).
    void init(rendering::GpuDevice& device,
              uint32_t backBufferFormat = 87u,
              uint32_t depthFormat     = 20u);

    // Add one flat-shade pass per loaded entity mesh into fg each render tick.
    // viewProj : row-major view*proj (16 floats, world space → clip space).
    // backBuffer / depthBuffer : handles imported into fg by the caller beforehand.
    // width / height : render target dimensions for viewport and scissor.
    void tick(core::ecs::World&        world,
              rendering::MeshManager&  meshManager,
              rendering::FrameGraph&   fg,
              const float              viewProj[16],
              rendering::ResourceHandle backBuffer,
              rendering::ResourceHandle depthBuffer,
              uint32_t                  width,
              uint32_t                  height);

    // Registers a GPU handle for the given entity index. Called from meshLoadFn.
    void registerHandle(uint32_t entityIndex, rendering::MeshHandle gpuHandle);

    // Releases all registered GPU handles. Called from meshUnloadFn.
    void clear();

private:
    std::unordered_map<uint32_t, rendering::MeshHandle> handles_;
    std::unique_ptr<rendering::FlatShadePipeline>        pipeline_;
};

} // namespace engine::app
