#include "core/math/Quat.h"

#include "core/math/Constants.h"
#include "core/math/Mat.h"
#include "core/math/Vec.h"

#include <cmath>

namespace engine::core::math {

    Quat fromAxisAngle(const Vec3& axis, float radians) noexcept {
        const float half = radians * 0.5f;
        const float s    = std::sin(half);
        return {s * axis.x, s * axis.y, s * axis.z, std::cos(half)};
    }

    Quat fromEulerYxz(float yaw, float pitch, float roll) noexcept {
        const Quat qY = fromAxisAngle({0.0f, 1.0f, 0.0f}, yaw);
        const Quat qX = fromAxisAngle({1.0f, 0.0f, 0.0f}, pitch);
        const Quat qZ = fromAxisAngle({0.0f, 0.0f, 1.0f}, roll);
        return qY * qX * qZ;
    }

    Vec3 rotate(const Quat& q, const Vec3& v) noexcept {
        // Optimized sandwich product: q * (0,v) * conj(q)
        const Vec3 qv  = {q.x, q.y, q.z};
        const Vec3 t   = cross(qv, v) * 2.0f;
        return v + t * q.w + cross(qv, t);
    }

    Mat4 toMat4(const Quat& q) noexcept {
        const float xx = q.x * q.x;
        const float yy = q.y * q.y;
        const float zz = q.z * q.z;
        const float xy = q.x * q.y;
        const float xz = q.x * q.z;
        const float yz = q.y * q.z;
        const float wx = q.w * q.x;
        const float wy = q.w * q.y;
        const float wz = q.w * q.z;

        // Row-major rotation matrix from unit quaternion.
        // Row i holds the image of basis vector i after rotation.
        Mat4 r{};
        r.m[0][0] = 1.0f - 2.0f * (yy + zz);
        r.m[0][1] =        2.0f * (xy + wz);
        r.m[0][2] =        2.0f * (xz - wy);
        r.m[0][3] = 0.0f;

        r.m[1][0] =        2.0f * (xy - wz);
        r.m[1][1] = 1.0f - 2.0f * (xx + zz);
        r.m[1][2] =        2.0f * (yz + wx);
        r.m[1][3] = 0.0f;

        r.m[2][0] =        2.0f * (xz + wy);
        r.m[2][1] =        2.0f * (yz - wx);
        r.m[2][2] = 1.0f - 2.0f * (xx + yy);
        r.m[2][3] = 0.0f;

        r.m[3][0] = 0.0f;
        r.m[3][1] = 0.0f;
        r.m[3][2] = 0.0f;
        r.m[3][3] = 1.0f;
        return r;
    }

    Quat fromMat4(const Mat4& m) noexcept {
        // Shepperd's method: branch on the largest diagonal element for numerical stability.
        const float trace = m.m[0][0] + m.m[1][1] + m.m[2][2];

        Quat q{};
        if (trace > 0.0f) {
            const float s = 0.5f / std::sqrt(trace + 1.0f);
            q.w = 0.25f / s;
            q.x = (m.m[1][2] - m.m[2][1]) * s;
            q.y = (m.m[2][0] - m.m[0][2]) * s;
            q.z = (m.m[0][1] - m.m[1][0]) * s;
        } else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
            const float s = 2.0f * std::sqrt(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]);
            q.w = (m.m[1][2] - m.m[2][1]) / s;
            q.x = 0.25f * s;
            q.y = (m.m[0][1] + m.m[1][0]) / s;
            q.z = (m.m[2][0] + m.m[0][2]) / s;
        } else if (m.m[1][1] > m.m[2][2]) {
            const float s = 2.0f * std::sqrt(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]);
            q.w = (m.m[2][0] - m.m[0][2]) / s;
            q.x = (m.m[0][1] + m.m[1][0]) / s;
            q.y = 0.25f * s;
            q.z = (m.m[1][2] + m.m[2][1]) / s;
        } else {
            const float s = 2.0f * std::sqrt(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]);
            q.w = (m.m[0][1] - m.m[1][0]) / s;
            q.x = (m.m[2][0] + m.m[0][2]) / s;
            q.y = (m.m[1][2] + m.m[2][1]) / s;
            q.z = 0.25f * s;
        }
        return normalize(q);
    }

    Quat slerp(const Quat& a, const Quat& b, float t) noexcept {
        float d = dot(a, b);

        // Take the short arc
        Quat bAdj = b;
        if (d < 0.0f) {
            bAdj = -b;
            d    = -d;
        }

        d = clamp(d, -1.0f, 1.0f);

        // Fall back to nlerp when nearly parallel to avoid division by ~0
        if (d > 0.9995f) {
            return nlerp(a, bAdj, t);
        }

        const float angle  = std::acos(d);
        const float sinAngle = std::sin(angle);
        const float wa = std::sin((1.0f - t) * angle) / sinAngle;
        const float wb = std::sin(t * angle)           / sinAngle;
        return normalize(a * wa + bAdj * wb);
    }

    Quat nlerp(const Quat& a, const Quat& b, float t) noexcept {
        // No binary Quat subtraction operator; express (b - a) as (b + (-a)).
        return normalize(a + (b + (-a)) * t);
    }

} // namespace engine::core::math
