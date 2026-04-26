#pragma once
#ifdef ENGINE_DEVREL

#include "core/ecs/World.h"
#include "core/ecs/Entity.h"

#include <cstdint>

namespace engine::tools {

class Editor {
public:
    Editor();
    ~Editor();

    // Called once at startup (after GpuDevice and ImGui DX12 backend are initialized).
    // device: ID3D12Device* as void*
    // numFramesInFlight: typically 2
    // backBufferFormat: DXGI_FORMAT value (87 = RGBA8_UNORM)
    // heap, cpuHandle, gpuHandle: descriptor heap info for ImGui font texture SRV slot
    void init(void* device, uint32_t numFramesInFlight, uint32_t backBufferFormat,
              void* heap, uint64_t cpuHandle, uint64_t gpuHandle);
    void shutdown();

    // Called each frame between FrameGraph::execute() and GpuDevice::endFrame().
    void update(core::ecs::World& world);

private:
    void drawEntityListPanel(core::ecs::World& world);
    void drawInspectorPanel(core::ecs::World& world);

    core::ecs::Entity selectedEntity_ = core::ecs::kInvalidEntity;
    bool initialized_ = false;
};

} // namespace engine::tools

#endif // ENGINE_DEVREL
