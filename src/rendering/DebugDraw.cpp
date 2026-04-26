#include <rendering/DebugDraw.h>
#include <core/math/Vec.h>
#include <core/math/Quat.h>
#include <core/log.h>

#include <vector>
#include <string>
#include <cmath>

// Provide the full definition for the forward-declared AABB.
namespace engine::core::math {
    struct AABB {
        Vec3 min;
        Vec3 max;
    };
}

namespace engine::rendering::DebugDraw {

namespace {

    struct LineEntry {
        core::math::Vec3 from;
        core::math::Vec3 to;
        core::math::Vec4 color;
    };

    struct TextEntry {
        core::math::Vec3 worldPos;
        core::math::Vec4 color;
        std::string      str;
    };

    constexpr uint32_t kMaxTextLabels = 256;

    std::vector<LineEntry> gLines;
    std::vector<TextEntry> gText;
    uint32_t               gMaxPrimitives  = 65536;
    bool                   gOverflowWarned = false;

    void pushLine(const core::math::Vec3& from,
                  const core::math::Vec3& to,
                  const core::math::Vec4& color)
    {
        if (gLines.size() >= gMaxPrimitives) {
            if (!gOverflowWarned) {
                LOG_WARN("DebugDraw: primitive limit ({}) exceeded; excess dropped", gMaxPrimitives);
                gOverflowWarned = true;
            }
            return;
        }
        gLines.push_back({from, to, color});
    }

} // anonymous namespace

// ---------------------------------------------------------------------------

void init(uint32_t maxPrimitives)
{
    gMaxPrimitives = (maxPrimitives == 0) ? 65536u : maxPrimitives;
    gLines.reserve(gMaxPrimitives);
    gText.reserve(kMaxTextLabels);
}

void shutdown()
{
    gLines.clear();
    gLines.shrink_to_fit();
    gText.clear();
    gText.shrink_to_fit();
    gOverflowWarned = false;
}

void flush()
{
    gLines.clear();
    gText.clear();
    gOverflowWarned = false;
}

// ---------------------------------------------------------------------------

void line(core::math::Vec3 start, core::math::Vec3 end, core::math::Vec4 color)
{
    pushLine(start, end, color);
}

void aabb(const core::math::AABB& b, core::math::Vec4 color)
{
    using core::math::Vec3;

    const Vec3& lo = b.min;
    const Vec3& hi = b.max;

    // 8 corners
    const Vec3 c[8] = {
        {lo.x, lo.y, lo.z},
        {hi.x, lo.y, lo.z},
        {hi.x, hi.y, lo.z},
        {lo.x, hi.y, lo.z},
        {lo.x, lo.y, hi.z},
        {hi.x, lo.y, hi.z},
        {hi.x, hi.y, hi.z},
        {lo.x, hi.y, hi.z},
    };

    // 12 edges: 4 bottom, 4 top, 4 verticals
    pushLine(c[0], c[1], color);
    pushLine(c[1], c[2], color);
    pushLine(c[2], c[3], color);
    pushLine(c[3], c[0], color);

    pushLine(c[4], c[5], color);
    pushLine(c[5], c[6], color);
    pushLine(c[6], c[7], color);
    pushLine(c[7], c[4], color);

    pushLine(c[0], c[4], color);
    pushLine(c[1], c[5], color);
    pushLine(c[2], c[6], color);
    pushLine(c[3], c[7], color);
}

void box(core::math::Vec3 center,
         core::math::Vec3 halfExtents,
         core::math::Quat rotation,
         core::math::Vec4 color)
{
    using core::math::Vec3;
    using core::math::rotate;

    // 8 local corners; rotate each into world space then offset by center
    const float hx = halfExtents.x;
    const float hy = halfExtents.y;
    const float hz = halfExtents.z;

    const Vec3 local[8] = {
        {-hx, -hy, -hz},
        { hx, -hy, -hz},
        { hx,  hy, -hz},
        {-hx,  hy, -hz},
        {-hx, -hy,  hz},
        { hx, -hy,  hz},
        { hx,  hy,  hz},
        {-hx,  hy,  hz},
    };

    Vec3 c[8];
    for (int i = 0; i < 8; ++i)
        c[i] = center + rotate(rotation, local[i]);

    pushLine(c[0], c[1], color);
    pushLine(c[1], c[2], color);
    pushLine(c[2], c[3], color);
    pushLine(c[3], c[0], color);

    pushLine(c[4], c[5], color);
    pushLine(c[5], c[6], color);
    pushLine(c[6], c[7], color);
    pushLine(c[7], c[4], color);

    pushLine(c[0], c[4], color);
    pushLine(c[1], c[5], color);
    pushLine(c[2], c[6], color);
    pushLine(c[3], c[7], color);
}

void sphere(core::math::Vec3 center,
            float            radius,
            core::math::Vec4 color)
{
    constexpr int segments = 16;
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float step = kTwoPi / static_cast<float>(segments);

    for (int i = 0; i < segments; ++i) {
        const float a0 = step * static_cast<float>(i);
        const float a1 = step * static_cast<float>(i + 1);
        const float c0 = std::cos(a0) * radius;
        const float s0 = std::sin(a0) * radius;
        const float c1 = std::cos(a1) * radius;
        const float s1 = std::sin(a1) * radius;

        // XY plane
        pushLine({center.x + c0, center.y + s0, center.z},
                 {center.x + c1, center.y + s1, center.z},
                 color);
        // YZ plane
        pushLine({center.x, center.y + c0, center.z + s0},
                 {center.x, center.y + c1, center.z + s1},
                 color);
        // XZ plane
        pushLine({center.x + c0, center.y, center.z + s0},
                 {center.x + c1, center.y, center.z + s1},
                 color);
    }
}

void text(core::math::Vec3  worldPos,
          std::string_view  str,
          core::math::Vec4  color)
{
    if (gText.size() >= kMaxTextLabels)
        return;
    gText.push_back({worldPos, color, std::string(str)});
}

// ---------------------------------------------------------------------------
// Read-only accessors used by the renderer's debug pass (internal linkage).
// The rendering backend includes this .cpp directly or calls these via the
// same translation unit. Declared here as file-scope helpers.

const std::vector<LineEntry>& debugLines() { return gLines; }
const std::vector<TextEntry>& debugText()  { return gText;  }

} // namespace engine::rendering::DebugDraw
