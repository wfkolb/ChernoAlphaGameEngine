#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "MeshRenderSystem.h"

#include <rendering/FlatShadePass.h>
#include <core/ecs/View.h>
#include <core/math/Mat.h>
#include <core/math/Transform.h>

// Internal rendering header — app layer may access rendering internals for pipeline setup.
#include <FlatShadePipeline.h>

#include <d3d12.h>

namespace engine::app {

MeshRenderSystem::MeshRenderSystem()  = default;
MeshRenderSystem::~MeshRenderSystem() = default;

void MeshRenderSystem::init(rendering::GpuDevice& device,
                             uint32_t backBufferFormat,
                             uint32_t depthFormat)
{
    auto* d3dDevice = static_cast<ID3D12Device*>(device.nativeDevice());
    pipeline_ = std::make_unique<rendering::FlatShadePipeline>(
        rendering::createFlatShadePipeline(d3dDevice, backBufferFormat, depthFormat));
}

void MeshRenderSystem::tick(core::ecs::World&         world,
                             rendering::MeshManager&   meshManager,
                             rendering::FrameGraph&    fg,
                             const float               viewProj[16],
                             rendering::ResourceHandle backBuffer,
                             rendering::ResourceHandle depthBuffer,
                             uint32_t                  width,
                             uint32_t                  height)
{
    if (!pipeline_) return;

    // Reconstruct viewProj as Mat4 once per tick (row-major, matches engine convention).
    core::math::Mat4 vp{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            vp.m[r][c] = viewProj[r * 4 + c];

    core::ecs::View<core::Transform, core::MeshHandle> view(world);
    for (auto [entity, transform, meshHandle] : view) {
        auto it = handles_.find(entity.index);
        if (it == handles_.end()) continue;

        const rendering::MeshHandle gpuHandle = it->second;
        if (!gpuHandle.isValid()) continue;

        // Build world matrix from the entity's transform.
        // core::Transform mirrors core::math::Transform field layout; convert manually.
        core::math::Transform mathTransform{};
        mathTransform.position = transform.position;
        mathTransform.rotation = transform.rotation;
        mathTransform.scale    = transform.scale;

        const core::math::Mat4 worldMat = mathTransform.toMatrix();
        const core::math::Mat4 mvp      = worldMat * vp;

        rendering::addFlatShadePass(
            fg,
            meshManager,
            gpuHandle,
            *pipeline_,
            &mvp.m[0][0],
            backBuffer,
            depthBuffer,
            width,
            height);
    }
}

void MeshRenderSystem::registerHandle(uint32_t entityIndex,
                                       rendering::MeshHandle gpuHandle)
{
    handles_[entityIndex] = gpuHandle;
}

void MeshRenderSystem::clear()
{
    handles_.clear();
}

} // namespace engine::app
