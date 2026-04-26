#pragma once

#include "core/math/Mat.h"
#include "core/math/Vec.h"

namespace engine::core::math {

    struct alignas(16) Quat {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
        float w{1.0f};

        constexpr Quat() = default;
        constexpr Quat(float xx, float yy, float zz, float ww) noexcept : x(xx), y(yy), z(zz), w(ww) {}

        static constexpr Quat identity() noexcept { return {0.0f, 0.0f, 0.0f, 1.0f}; }

        constexpr bool operator==(const Quat& r) const noexcept {
            return x == r.x && y == r.y && z == r.z && w == r.w;
        }
        constexpr bool operator!=(const Quat& r) const noexcept { return !(*this == r); }
    };

    constexpr Quat operator*(const Quat& a, const Quat& b) noexcept {
        return {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        };
    }

    constexpr Quat operator*(const Quat& q, float s) noexcept { return {q.x * s, q.y * s, q.z * s, q.w * s}; }
    constexpr Quat operator+(const Quat& a, const Quat& b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
    constexpr Quat operator-(const Quat& q) noexcept { return {-q.x, -q.y, -q.z, -q.w}; }

    constexpr float dot(const Quat& a, const Quat& b) noexcept {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    inline float length(const Quat& q) noexcept { return std::sqrt(dot(q, q)); }

    inline Quat normalize(const Quat& q) noexcept {
        const float ls = dot(q, q);
        if (ls < kEpsilonNormalSq) return Quat::identity();
        const float inv = 1.0f / std::sqrt(ls);
        return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
    }

    constexpr Quat conjugate(const Quat& q) noexcept { return {-q.x, -q.y, -q.z, q.w}; }

    Quat fromAxisAngle(const Vec3& axis, float radians) noexcept;
    Quat fromEulerYxz(float yaw, float pitch, float roll) noexcept;

    Vec3 rotate(const Quat& q, const Vec3& v) noexcept;

    Mat4 toMat4(const Quat& q) noexcept;
    Quat fromMat4(const Mat4& m) noexcept;

    Quat slerp(const Quat& a, const Quat& b, float t) noexcept;
    Quat nlerp(const Quat& a, const Quat& b, float t) noexcept;

}
