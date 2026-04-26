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

    // Flush all accumulated debug geometry into the frame graph's debug pass.
    // Called automatically by the DebugFlush system in the PostRender phase.
    void flush();

    // Allocate internal CPU buffers. maxPrimitives hard-caps geometry per frame;
    // excess is dropped with a LOG_WARN (once per frame).
    void init(uint32_t maxPrimitives = 65536);

    // Release all internal buffers. Call during renderer shutdown.
    void shutdown();

} // namespace engine::rendering::DebugDraw
