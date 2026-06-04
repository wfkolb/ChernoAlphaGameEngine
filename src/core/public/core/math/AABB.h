#pragma once

#include "core/math/Vec.h"
#include <limits>

namespace engine::core::math {

struct AABB {
    Vec3 min = { std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max() };
    Vec3 max = { -std::numeric_limits<float>::max(),
                 -std::numeric_limits<float>::max(),
                 -std::numeric_limits<float>::max() };

    static AABB fromCenterExtents(const Vec3& center, const Vec3& halfExtents) noexcept {
        return { center - halfExtents, center + halfExtents };
    }

    bool isValid() const noexcept {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }

    Vec3 center()  const noexcept { return (min + max) * 0.5f; }
    Vec3 extents() const noexcept { return (max - min) * 0.5f; }
    Vec3 size()    const noexcept { return max - min; }

    float surfaceArea() const noexcept {
        const Vec3 s = size();
        return 2.0f * (s.x * s.y + s.y * s.z + s.z * s.x);
    }

    void expand(const Vec3& p) noexcept {
        min = minPerComponent(min, p);
        max = maxPerComponent(max, p);
    }

    void expand(const AABB& other) noexcept {
        min = minPerComponent(min, other.min);
        max = maxPerComponent(max, other.max);
    }

    void expandBy(float margin) noexcept {
        const Vec3 m = { margin, margin, margin };
        min = min - m;
        max = max + m;
    }

    bool contains(const Vec3& p) const noexcept {
        return p.x >= min.x && p.x <= max.x
            && p.y >= min.y && p.y <= max.y
            && p.z >= min.z && p.z <= max.z;
    }

    bool intersects(const AABB& other) const noexcept {
        return max.x >= other.min.x && min.x <= other.max.x
            && max.y >= other.min.y && min.y <= other.max.y
            && max.z >= other.min.z && min.z <= other.max.z;
    }

    // Slab-method ray test. Returns hit t (>= 0) or -1 on miss.
    float raycast(const Vec3& origin, const Vec3& dir) const noexcept {
        const Vec3 invDir = {
            dir.x != 0.0f ? 1.0f / dir.x : std::numeric_limits<float>::max(),
            dir.y != 0.0f ? 1.0f / dir.y : std::numeric_limits<float>::max(),
            dir.z != 0.0f ? 1.0f / dir.z : std::numeric_limits<float>::max(),
        };

        float tx1 = (min.x - origin.x) * invDir.x;
        float tx2 = (max.x - origin.x) * invDir.x;
        float tmin = tx1 < tx2 ? tx1 : tx2;
        float tmax = tx1 > tx2 ? tx1 : tx2;

        float ty1 = (min.y - origin.y) * invDir.y;
        float ty2 = (max.y - origin.y) * invDir.y;
        float tyMin = ty1 < ty2 ? ty1 : ty2;
        float tyMax = ty1 > ty2 ? ty1 : ty2;
        tmin = tyMin > tmin ? tyMin : tmin;
        tmax = tyMax < tmax ? tyMax : tmax;

        float tz1 = (min.z - origin.z) * invDir.z;
        float tz2 = (max.z - origin.z) * invDir.z;
        float tzMin = tz1 < tz2 ? tz1 : tz2;
        float tzMax = tz1 > tz2 ? tz1 : tz2;
        tmin = tzMin > tmin ? tzMin : tmin;
        tmax = tzMax < tmax ? tzMax : tmax;

        if (tmax < 0.0f || tmin > tmax) return -1.0f;
        return tmin >= 0.0f ? tmin : 0.0f;
    }
};

inline AABB merge(const AABB& a, const AABB& b) noexcept {
    AABB result;
    result.min = minPerComponent(a.min, b.min);
    result.max = maxPerComponent(a.max, b.max);
    return result;
}

} // namespace engine::core::math
