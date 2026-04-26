#include "core/math/Frustum.h"

#include "core/math/Constants.h"

#include <cmath>

namespace engine::core::math {

    // ---------------------------------------------------------------------------
    // frustumFromViewProj
    // ---------------------------------------------------------------------------
    // For a row-major matrix with row-vector convention (v' = v * M), the clip-space
    // coordinate of a world-space point p is:
    //
    //     (px, py, pz, 1) * VP  =  (cx, cy, cz, cw)
    //
    // A point is inside the frustum when -cw <= cx,cy,cz <= cw  (homogeneous clip).
    // With reverse-Z the near condition is cz <= cw (near=1.0) and far is cz >= 0 (far=0.0).
    //
    // Writing "row_i" for the i-th row of VP (as a Vec4), the six inequalities become:
    //
    //   Left:   p · (row3 + row0) >= 0
    //   Right:  p · (row3 - row0) >= 0
    //   Bottom: p · (row3 + row1) >= 0
    //   Top:    p · (row3 - row1) >= 0
    //   Near:   p · row2          >= 0   (reverse-Z: cz <= cw  → cw - cz >= 0)
    //   Far:    p · (row3 - row2) >= 0   (cz >= 0)
    //
    // Each resulting Vec4 is normalized so that (nx,ny,nz) is a unit vector and
    // d is scaled by the same factor.
    // ---------------------------------------------------------------------------

    static Vec4 makePlane(float nx, float ny, float nz, float d) noexcept {
        const float lenSq = nx * nx + ny * ny + nz * nz;
        if (lenSq < kEpsilonNormalSq) {
            return {0.0f, 0.0f, 0.0f, 0.0f};
        }
        const float inv = 1.0f / std::sqrt(lenSq);
        return {nx * inv, ny * inv, nz * inv, d * inv};
    }

    Frustum frustumFromViewProj(const Mat4& vp) noexcept {
        // Each row of vp is vp.m[row][0..3].
        // row0 = (m00, m01, m02, m03)
        // row1 = (m10, m11, m12, m13)
        // row2 = (m20, m21, m22, m23)
        // row3 = (m30, m31, m32, m33)

        Frustum f{};

        // Left:   row3 + row0
        f.planes[Frustum::kLeft] = makePlane(
            vp.m[3][0] + vp.m[0][0],
            vp.m[3][1] + vp.m[0][1],
            vp.m[3][2] + vp.m[0][2],
            vp.m[3][3] + vp.m[0][3]);

        // Right:  row3 - row0
        f.planes[Frustum::kRight] = makePlane(
            vp.m[3][0] - vp.m[0][0],
            vp.m[3][1] - vp.m[0][1],
            vp.m[3][2] - vp.m[0][2],
            vp.m[3][3] - vp.m[0][3]);

        // Bottom: row3 + row1
        f.planes[Frustum::kBottom] = makePlane(
            vp.m[3][0] + vp.m[1][0],
            vp.m[3][1] + vp.m[1][1],
            vp.m[3][2] + vp.m[1][2],
            vp.m[3][3] + vp.m[1][3]);

        // Top:    row3 - row1
        f.planes[Frustum::kTop] = makePlane(
            vp.m[3][0] - vp.m[1][0],
            vp.m[3][1] - vp.m[1][1],
            vp.m[3][2] - vp.m[1][2],
            vp.m[3][3] - vp.m[1][3]);

        // Near:   row2  (reverse-Z: depth at near = 1.0 → cz <= cw → cw - cz >= 0)
        f.planes[Frustum::kNear] = makePlane(
            vp.m[2][0],
            vp.m[2][1],
            vp.m[2][2],
            vp.m[2][3]);

        // Far:    row3 - row2  (reverse-Z: depth at far = 0.0 → cz >= 0)
        f.planes[Frustum::kFar] = makePlane(
            vp.m[3][0] - vp.m[2][0],
            vp.m[3][1] - vp.m[2][1],
            vp.m[3][2] - vp.m[2][2],
            vp.m[3][3] - vp.m[2][3]);

        return f;
    }

    // ---------------------------------------------------------------------------
    // frustumContainsSphere
    // ---------------------------------------------------------------------------
    // A sphere (center, radius) is fully outside a plane when the signed distance
    // of the center to the plane is less than -radius.
    // Signed distance = dot(plane.xyz, center) + plane.w
    // (plane.xyz is already unit-length after normalization above)
    // ---------------------------------------------------------------------------

    bool frustumContainsSphere(const Frustum& f, Vec3 center, float radius) noexcept {
        for (int i = 0; i < 6; ++i) {
            const Vec4& p = f.planes[i];
            const float dist = p.x * center.x + p.y * center.y + p.z * center.z + p.w;
            if (dist < -radius) {
                return false;
            }
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // frustumContainsAabb
    // ---------------------------------------------------------------------------
    // For each plane, find the AABB corner that is furthest in the negative normal
    // direction (the "negative vertex" n-vertex). If even that corner is on the
    // outside (negative side), the entire AABB is outside and we can reject it.
    // ---------------------------------------------------------------------------

    bool frustumContainsAabb(const Frustum& f, Vec3 min, Vec3 max) noexcept {
        for (int i = 0; i < 6; ++i) {
            const Vec4& p = f.planes[i];

            // Pick the component of the AABB that gives the smallest (most negative)
            // dot product with the plane normal — the negative vertex.
            const float nx = (p.x >= 0.0f) ? min.x : max.x;
            const float ny = (p.y >= 0.0f) ? min.y : max.y;
            const float nz = (p.z >= 0.0f) ? min.z : max.z;

            const float dist = p.x * nx + p.y * ny + p.z * nz + p.w;
            if (dist < 0.0f) {
                return false;
            }
        }
        return true;
    }

} // namespace engine::core::math
