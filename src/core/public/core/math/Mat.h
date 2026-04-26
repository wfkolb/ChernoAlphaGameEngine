#pragma once

#include "core/math/Vec.h"

namespace engine::core::math {

    // Row-major 4x4 matrix. m[row][col] indexing.
    // Vectors are treated as row vectors: v' = v * M.
    // Matches HLSL's mul(v, M) semantics when M is uploaded as-is.
    struct alignas(16) Mat4 {
        float m[4][4]{};

        constexpr Mat4() = default;

        constexpr Mat4(float m00, float m01, float m02, float m03,
                       float m10, float m11, float m12, float m13,
                       float m20, float m21, float m22, float m23,
                       float m30, float m31, float m32, float m33) noexcept
            : m{
                  {m00, m01, m02, m03},
                  {m10, m11, m12, m13},
                  {m20, m21, m22, m23},
                  {m30, m31, m32, m33},
              } {}

        static constexpr Mat4 identity() noexcept {
            return Mat4{
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
            };
        }

        static constexpr Mat4 zero() noexcept { return Mat4{}; }

        constexpr bool operator==(const Mat4& r) const noexcept {
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    if (m[i][j] != r.m[i][j]) return false;
            return true;
        }
        constexpr bool operator!=(const Mat4& r) const noexcept { return !(*this == r); }
    };

    constexpr Mat4 operator*(const Mat4& a, const Mat4& b) noexcept {
        Mat4 r{};
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                r.m[i][j] = a.m[i][0] * b.m[0][j]
                          + a.m[i][1] * b.m[1][j]
                          + a.m[i][2] * b.m[2][j]
                          + a.m[i][3] * b.m[3][j];
            }
        }
        return r;
    }

    // Row-vector × matrix:  v' = v * M
    constexpr Vec4 operator*(const Vec4& v, const Mat4& m) noexcept {
        return {
            v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + v.w * m.m[3][0],
            v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + v.w * m.m[3][1],
            v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + v.w * m.m[3][2],
            v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + v.w * m.m[3][3],
        };
    }

    constexpr Vec3 transformPoint(const Vec3& p, const Mat4& m) noexcept {
        const Vec4 r = Vec4{p, 1.0f} * m;
        const float invW = (r.w != 0.0f) ? 1.0f / r.w : 1.0f;
        return {r.x * invW, r.y * invW, r.z * invW};
    }

    constexpr Vec3 transformDirection(const Vec3& d, const Mat4& m) noexcept {
        const Vec4 r = Vec4{d, 0.0f} * m;
        return {r.x, r.y, r.z};
    }

    Mat4 transpose(const Mat4& m) noexcept;
    Mat4 inverse(const Mat4& m, bool* outOk = nullptr) noexcept;
    Mat4 affineInverse(const Mat4& m) noexcept;
    float determinant(const Mat4& m) noexcept;

    Mat4 translation(const Vec3& t) noexcept;
    Mat4 scaling(const Vec3& s) noexcept;
    Mat4 rotationX(float radians) noexcept;
    Mat4 rotationY(float radians) noexcept;
    Mat4 rotationZ(float radians) noexcept;

    // Right-handed, +Z forward, Y-up. Reverse-Z (near maps to depth=1, far maps to 0).
    Mat4 perspectiveRhYupReverseZ(float fovYRadians, float aspect, float nearZ, float farZ) noexcept;

    // Right-handed, +Z forward, Y-up, depth in [0,1] (DX convention).
    Mat4 perspectiveRhYup(float fovYRadians, float aspect, float nearZ, float farZ) noexcept;

    Mat4 orthographicRhYup(float left, float right, float bottom, float top, float nearZ, float farZ) noexcept;

    // Right-handed lookAt. Camera +Z points from target toward eye (backward); objects in front have negative view-space z.
    Mat4 lookAtRh(const Vec3& eye, const Vec3& target, const Vec3& worldUp) noexcept;

}
