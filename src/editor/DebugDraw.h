#pragma once
#ifdef ENGINE_DEVREL

#include <core/math/Vec.h>
#include <core/math/Mat.h>
#include <cstdint>

// Forward-declare in global scope so flush() accepts ::ImDrawList*, not
// a separate engine::editor::ImDrawList introduced by a namespace-scoped decl.
struct ImDrawList;

namespace engine::editor {

// Immediate-mode batched line-list debug renderer. Lines are submitted each
// frame and drawn via an ImGui draw-list overlay. Not a persistent buffer.
//
// Usage:
//   DebugDraw dd;
//   dd.line({0,0,0}, {1,0,0}, 0xFF0000FF);    // red
//   dd.wireBox(center, halfExtents, 0xFF00FF00); // green
//   dd.flush(drawList, viewProj, originX, originY, w, h);
class DebugDraw {
public:
    static constexpr size_t kMaxLines = 50000;

    // RGBA packed as 0xAABBGGRR (ImGui convention).
    void line(core::math::Vec3 a, core::math::Vec3 b, uint32_t colour);

    void wireBox(core::math::Vec3 center,
                 core::math::Vec3 halfExtents,
                 uint32_t colour);

    void wireSphere(core::math::Vec3 center, float radius, uint32_t colour);

    // radius = hemisphere radius; halfHeight = half the cylindrical section.
    void wireCapsule(core::math::Vec3 center, float radius, float halfHeight,
                     uint32_t colour);

    // Submit all pending lines to an ImGui draw list.
    // viewProj: combined view-projection matrix (row-major, matches SpinDemo convention).
    // originX/Y: screen-space top-left of the viewport image.
    // vpW/vpH: viewport image size in pixels.
    void flush(ImDrawList* dl,
               const core::math::Mat4& viewProj,
               float originX, float originY,
               float vpW, float vpH);

    void clear();

private:
    struct Line {
        core::math::Vec3 a, b;
        uint32_t colour;
    };

    Line   lines_[kMaxLines];
    size_t count_ = 0;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
