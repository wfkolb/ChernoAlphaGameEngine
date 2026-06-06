#ifdef ENGINE_DEVREL

#include "editor/DebugDraw.h"

#include <imgui.h>

#include <cmath>

namespace engine::editor {

namespace {

// Project a world-space point into viewport pixel coordinates.
// Returns false if the point is behind the near plane.
bool projectPoint(const core::math::Vec3& p,
                  const core::math::Mat4& vp,
                  float originX, float originY,
                  float vpW, float vpH,
                  float& outX, float& outY) {
    // Row-vector convention: p_clip = [p.x, p.y, p.z, 1] * vp
    const float cx = p.x * vp.m[0][0] + p.y * vp.m[1][0] + p.z * vp.m[2][0] + vp.m[3][0];
    const float cy = p.x * vp.m[0][1] + p.y * vp.m[1][1] + p.z * vp.m[2][1] + vp.m[3][1];
    const float cw = p.x * vp.m[0][3] + p.y * vp.m[1][3] + p.z * vp.m[2][3] + vp.m[3][3];

    if (cw <= 0.f) return false; // behind camera

    const float invW = 1.f / cw;
    const float ndcX =  cx * invW;
    const float ndcY = -cy * invW; // flip Y: NDC +Y is up, screen +Y is down

    outX = originX + (ndcX * 0.5f + 0.5f) * vpW;
    outY = originY + (ndcY * 0.5f + 0.5f) * vpH;
    return true;
}

} // anonymous namespace

void DebugDraw::line(core::math::Vec3 a, core::math::Vec3 b, uint32_t colour) {
    if (count_ >= kMaxLines) return;
    lines_[count_++] = { a, b, colour };
}

void DebugDraw::wireBox(core::math::Vec3 c, core::math::Vec3 h, uint32_t col) {
    // 8 corners
    const core::math::Vec3 corners[8] = {
        { c.x - h.x, c.y - h.y, c.z - h.z },
        { c.x + h.x, c.y - h.y, c.z - h.z },
        { c.x + h.x, c.y - h.y, c.z + h.z },
        { c.x - h.x, c.y - h.y, c.z + h.z },
        { c.x - h.x, c.y + h.y, c.z - h.z },
        { c.x + h.x, c.y + h.y, c.z - h.z },
        { c.x + h.x, c.y + h.y, c.z + h.z },
        { c.x - h.x, c.y + h.y, c.z + h.z },
    };
    // Bottom face
    line(corners[0], corners[1], col); line(corners[1], corners[2], col);
    line(corners[2], corners[3], col); line(corners[3], corners[0], col);
    // Top face
    line(corners[4], corners[5], col); line(corners[5], corners[6], col);
    line(corners[6], corners[7], col); line(corners[7], corners[4], col);
    // Verticals
    line(corners[0], corners[4], col); line(corners[1], corners[5], col);
    line(corners[2], corners[6], col); line(corners[3], corners[7], col);
}

void DebugDraw::wireSphere(core::math::Vec3 c, float r, uint32_t col) {
    constexpr int kSegs = 24;
    constexpr float kTwoPi = 6.28318530f;
    // XY, YZ, XZ great circles
    for (int seg = 0; seg < kSegs; ++seg) {
        const float t0 = static_cast<float>(seg)     / kSegs * kTwoPi;
        const float t1 = static_cast<float>(seg + 1) / kSegs * kTwoPi;
        const float c0 = std::cos(t0), s0 = std::sin(t0);
        const float c1 = std::cos(t1), s1 = std::sin(t1);

        line({ c.x + r * c0, c.y + r * s0, c.z }, { c.x + r * c1, c.y + r * s1, c.z }, col);
        line({ c.x,         c.y + r * c0, c.z + r * s0 }, { c.x, c.y + r * c1, c.z + r * s1 }, col);
        line({ c.x + r * c0, c.y, c.z + r * s0 }, { c.x + r * c1, c.y, c.z + r * s1 }, col);
    }
}

void DebugDraw::wireCapsule(core::math::Vec3 c, float r, float hh, uint32_t col) {
    constexpr int kSegs = 16;
    constexpr float kPi    = 3.14159265f;
    constexpr float kTwoPi = 6.28318530f;

    // Cylindrical band — 4 vertical lines + 2 circular rings
    const core::math::Vec3 top    = { c.x, c.y + hh, c.z };
    const core::math::Vec3 bottom = { c.x, c.y - hh, c.z };

    for (int seg = 0; seg < kSegs; ++seg) {
        const float t0 = static_cast<float>(seg)     / kSegs * kTwoPi;
        const float t1 = static_cast<float>(seg + 1) / kSegs * kTwoPi;
        const float c0 = std::cos(t0), s0 = std::sin(t0);
        const float c1 = std::cos(t1), s1 = std::sin(t1);

        // Top ring
        line({ top.x + r*c0, top.y, top.z + r*s0 }, { top.x + r*c1, top.y, top.z + r*s1 }, col);
        // Bottom ring
        line({ bottom.x + r*c0, bottom.y, bottom.z + r*s0 }, { bottom.x + r*c1, bottom.y, bottom.z + r*s1 }, col);
    }
    // 4 vertical lines
    for (int i = 0; i < 4; ++i) {
        const float a = static_cast<float>(i) / 4.f * kTwoPi;
        const float ca = std::cos(a), sa = std::sin(a);
        line({ top.x + r*ca, top.y, top.z + r*sa },
             { bottom.x + r*ca, bottom.y, bottom.z + r*sa }, col);
    }
    // Top hemisphere — XZ semicircle
    for (int seg = 0; seg <= kSegs / 2; ++seg) {
        const float t0 = static_cast<float>(seg)     / kSegs * kPi;
        const float t1 = static_cast<float>(seg + 1) / kSegs * kPi;
        line({ top.x + r*std::cos(t0), top.y + r*std::sin(t0), top.z },
             { top.x + r*std::cos(t1), top.y + r*std::sin(t1), top.z }, col);
        line({ top.x, top.y + r*std::sin(t0), top.z + r*std::cos(t0) },
             { top.x, top.y + r*std::sin(t1), top.z + r*std::cos(t1) }, col);
    }
    // Bottom hemisphere
    for (int seg = 0; seg <= kSegs / 2; ++seg) {
        const float t0 = static_cast<float>(seg)     / kSegs * kPi + kPi;
        const float t1 = static_cast<float>(seg + 1) / kSegs * kPi + kPi;
        line({ bottom.x + r*std::cos(t0), bottom.y + r*std::sin(t0), bottom.z },
             { bottom.x + r*std::cos(t1), bottom.y + r*std::sin(t1), bottom.z }, col);
        line({ bottom.x, bottom.y + r*std::sin(t0), bottom.z + r*std::cos(t0) },
             { bottom.x, bottom.y + r*std::sin(t1), bottom.z + r*std::cos(t1) }, col);
    }
}

void DebugDraw::flush(ImDrawList* dl,
                      const core::math::Mat4& viewProj,
                      float originX, float originY,
                      float vpW, float vpH) {
    for (size_t i = 0; i < count_; ++i) {
        const auto& ln = lines_[i];
        float ax, ay, bx, by;
        const bool aOk = projectPoint(ln.a, viewProj, originX, originY, vpW, vpH, ax, ay);
        const bool bOk = projectPoint(ln.b, viewProj, originX, originY, vpW, vpH, bx, by);
        if (aOk && bOk) {
            dl->AddLine({ ax, ay }, { bx, by }, ln.colour, 1.5f);
        }
    }
}

void DebugDraw::clear() { count_ = 0; }

} // namespace engine::editor

#endif // ENGINE_DEVREL
