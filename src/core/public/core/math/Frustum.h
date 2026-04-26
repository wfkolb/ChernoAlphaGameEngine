#pragma once

#include "core/math/Mat.h"
#include "core/math/Vec.h"

namespace engine::core::math {

    // A view frustum represented as 6 planes in world space.
    // Each plane is stored as Vec4(nx, ny, nz, d) where the plane equation is:
    //     nx*x + ny*y + nz*z + d >= 0   (points on the positive / inside half-space)
    // The normal (nx, ny, nz) is unit-length; d has been scaled accordingly.
    //
    // Math conventions:
    //   - Right-handed, Y-up, +Z forward
    //   - Row-major Mat4, row-vector convention: v' = v * M
    //   - Reverse-Z depth: near plane maps to depth 1.0, far plane maps to depth 0.0
    struct Frustum {
        // Plane indices
        enum : int {
            kLeft   = 0,
            kRight  = 1,
            kBottom = 2,
            kTop    = 3,
            kNear   = 4,
            kFar    = 5,
        };

        Vec4 planes[6]{};
    };

    // Extracts and normalizes the 6 frustum planes from a combined view-projection matrix.
    // Uses Gribb/Hartmann plane extraction adapted for row-vector / row-major convention.
    Frustum frustumFromViewProj(const Mat4& vp) noexcept;

    // Returns true if the sphere is not fully outside any frustum plane (i.e. potentially visible).
    bool frustumContainsSphere(const Frustum& f, Vec3 center, float radius) noexcept;

    // Returns true if the AABB [min, max] is not fully outside any frustum plane.
    // Conservative test: uses the "negative vertex" (worst-case corner) for each plane.
    bool frustumContainsAabb(const Frustum& f, Vec3 min, Vec3 max) noexcept;

} // namespace engine::core::math
