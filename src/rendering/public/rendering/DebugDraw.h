#pragma once

#include <core/math/Vec.h>
#include <core/math/Quat.h>
#include <cstdint>
#include <string_view>

// AABB lives in core/math — forward-declare so this header has no dependency on
// a potentially large math header that may not exist yet.
namespace engine::core::math { struct AABB; }

namespace engine::rendering::DebugDraw {

    // Draw a line segment in world space.
    void line(core::math::Vec3 start,
              core::math::Vec3 end,
              core::math::Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});

    // Draw a wireframe sphere.
    void sphere(core::math::Vec3 center,
                float            radius,
                core::math::Vec4 color = {1.0f, 1.0f, 0.0f, 1.0f});

    // Draw a wireframe oriented box.
    void box(core::math::Vec3 center,
             core::math::Vec3 halfExtents,
             core::math::Quat rotation,
             core::math::Vec4 color = {0.0f, 1.0f, 1.0f, 1.0f});

    // Draw a wireframe axis-aligned bounding box.
    void aabb(const core::math::AABB& aabb,
              core::math::Vec4        color = {0.0f, 1.0f, 0.0f, 1.0f});

    // Draw a world-space text label (screen-space billboard, bitmap font 8x8).
    // Up to 256 text labels per frame; excess is silently dropped.
    void text(core::math::Vec3  worldPos,
              std::string_view  str,
              core::math::Vec4  color = {1.0f, 1.0f, 1.0f, 1.0f});

    // Flush all accumulated debug geometry into the active command list.
    // cmdList     : ID3D12GraphicsCommandList* as void*
    // viewProj    : row-major 4x4 view-projection matrix (16 floats)
    // rtvCpuHandle: D3D12_CPU_DESCRIPTOR_HANDLE.ptr of the render target
    // width/height: render target dimensions in pixels
    void flush(void*         cmdList,
               const float   viewProj[16],
               uint64_t      rtvCpuHandle,
               uint32_t      width,
               uint32_t      height);

    // Allocate internal CPU buffers. maxPrimitives hard-caps geometry per frame;
    // excess is dropped with a LOG_WARN (once per frame).
    void init(uint32_t maxPrimitives = 65536);

    // Create GPU pipeline state (root signature, PSO, upload buffer).
    // Call after init() and after the GpuDevice is valid.
    // d3d12Device: ID3D12Device* as void*
    void initGpu(void* d3d12Device);

    // Release all internal buffers. Call during renderer shutdown.
    void shutdown();

} // namespace engine::rendering::DebugDraw
