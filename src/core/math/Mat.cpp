#include "core/math/Mat.h"

#include "core/math/Constants.h"
#include "core/math/Vec.h"

#include <cmath>

namespace engine::core::math {

    Mat4 transpose(const Mat4& a) noexcept {
        Mat4 r{};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                r.m[i][j] = a.m[j][i];
        return r;
    }

    float determinant(const Mat4& a) noexcept {
        // Laplace cofactor expansion along row 0
        auto minor3 = [&](int r0, int r1, int r2, int c0, int c1, int c2) -> float {
            const float* rows[3] = { &a.m[r0][0], &a.m[r1][0], &a.m[r2][0] };
            return rows[0][c0] * (rows[1][c1] * rows[2][c2] - rows[1][c2] * rows[2][c1])
                 - rows[0][c1] * (rows[1][c0] * rows[2][c2] - rows[1][c2] * rows[2][c0])
                 + rows[0][c2] * (rows[1][c0] * rows[2][c1] - rows[1][c1] * rows[2][c0]);
        };

        return a.m[0][0] * minor3(1, 2, 3, 1, 2, 3)
             - a.m[0][1] * minor3(1, 2, 3, 0, 2, 3)
             + a.m[0][2] * minor3(1, 2, 3, 0, 1, 3)
             - a.m[0][3] * minor3(1, 2, 3, 0, 1, 2);
    }

    Mat4 inverse(const Mat4& a, bool* outOk) noexcept {
        // Adjugate / cofactor method
        auto minor3 = [&](int r0, int r1, int r2, int c0, int c1, int c2) -> float {
            return a.m[r0][c0] * (a.m[r1][c1] * a.m[r2][c2] - a.m[r1][c2] * a.m[r2][c1])
                 - a.m[r0][c1] * (a.m[r1][c0] * a.m[r2][c2] - a.m[r1][c2] * a.m[r2][c0])
                 + a.m[r0][c2] * (a.m[r1][c0] * a.m[r2][c1] - a.m[r1][c1] * a.m[r2][c0]);
        };

        // Cofactor matrix (not yet transposed)
        Mat4 cof{};
        cof.m[0][0] =  minor3(1, 2, 3, 1, 2, 3);
        cof.m[0][1] = -minor3(1, 2, 3, 0, 2, 3);
        cof.m[0][2] =  minor3(1, 2, 3, 0, 1, 3);
        cof.m[0][3] = -minor3(1, 2, 3, 0, 1, 2);

        cof.m[1][0] = -minor3(0, 2, 3, 1, 2, 3);
        cof.m[1][1] =  minor3(0, 2, 3, 0, 2, 3);
        cof.m[1][2] = -minor3(0, 2, 3, 0, 1, 3);
        cof.m[1][3] =  minor3(0, 2, 3, 0, 1, 2);

        cof.m[2][0] =  minor3(0, 1, 3, 1, 2, 3);
        cof.m[2][1] = -minor3(0, 1, 3, 0, 2, 3);
        cof.m[2][2] =  minor3(0, 1, 3, 0, 1, 3);
        cof.m[2][3] = -minor3(0, 1, 3, 0, 1, 2);

        cof.m[3][0] = -minor3(0, 1, 2, 1, 2, 3);
        cof.m[3][1] =  minor3(0, 1, 2, 0, 2, 3);
        cof.m[3][2] = -minor3(0, 1, 2, 0, 1, 3);
        cof.m[3][3] =  minor3(0, 1, 2, 0, 1, 2);

        const float det = a.m[0][0] * cof.m[0][0]
                        + a.m[0][1] * cof.m[0][1]
                        + a.m[0][2] * cof.m[0][2]
                        + a.m[0][3] * cof.m[0][3];

        const float absDet = det < 0.0f ? -det : det;
        if (absDet < kEpsilon) {
            if (outOk) *outOk = false;
            return Mat4::identity();
        }

        if (outOk) *outOk = true;
        const float invDet = 1.0f / det;

        // Adjugate = transpose of cofactor matrix
        Mat4 adj{};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                adj.m[i][j] = cof.m[j][i] * invDet;

        return adj;
    }

    Mat4 affineInverse(const Mat4& a) noexcept {
        // Upper-left 3x3 is assumed orthonormal rotation R; its inverse is its transpose.
        // Translation is in row 3.
        const Vec3 right = {a.m[0][0], a.m[0][1], a.m[0][2]};
        const Vec3 up    = {a.m[1][0], a.m[1][1], a.m[1][2]};
        const Vec3 fwd   = {a.m[2][0], a.m[2][1], a.m[2][2]};
        const Vec3 t     = {a.m[3][0], a.m[3][1], a.m[3][2]};

        Mat4 r{};
        r.m[0][0] = right.x;  r.m[0][1] = up.x;  r.m[0][2] = fwd.x;  r.m[0][3] = 0.0f;
        r.m[1][0] = right.y;  r.m[1][1] = up.y;  r.m[1][2] = fwd.y;  r.m[1][3] = 0.0f;
        r.m[2][0] = right.z;  r.m[2][1] = up.z;  r.m[2][2] = fwd.z;  r.m[2][3] = 0.0f;
        r.m[3][0] = -dot(right, t);
        r.m[3][1] = -dot(up,    t);
        r.m[3][2] = -dot(fwd,   t);
        r.m[3][3] = 1.0f;
        return r;
    }

    Mat4 translation(const Vec3& t) noexcept {
        Mat4 r = Mat4::identity();
        r.m[3][0] = t.x;
        r.m[3][1] = t.y;
        r.m[3][2] = t.z;
        return r;
    }

    Mat4 scaling(const Vec3& s) noexcept {
        Mat4 r{};
        r.m[0][0] = s.x;
        r.m[1][1] = s.y;
        r.m[2][2] = s.z;
        r.m[3][3] = 1.0f;
        return r;
    }

    Mat4 rotationX(float radians) noexcept {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        Mat4 r = Mat4::identity();
        r.m[1][1] =  c;
        r.m[1][2] =  s;
        r.m[2][1] = -s;
        r.m[2][2] =  c;
        return r;
    }

    Mat4 rotationY(float radians) noexcept {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        Mat4 r = Mat4::identity();
        r.m[0][0] =  c;
        r.m[0][2] = -s;
        r.m[2][0] =  s;
        r.m[2][2] =  c;
        return r;
    }

    Mat4 rotationZ(float radians) noexcept {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        Mat4 r = Mat4::identity();
        r.m[0][0] =  c;
        r.m[0][1] =  s;
        r.m[1][0] = -s;
        r.m[1][1] =  c;
        return r;
    }

    Mat4 perspectiveRhYupReverseZ(float fovYRadians, float aspect, float nearZ, float farZ) noexcept {
        const float f = 1.0f / std::tan(fovYRadians * 0.5f);
        Mat4 r{};
        r.m[0][0] = f / aspect;
        r.m[1][1] = f;
        r.m[2][2] = nearZ / (farZ - nearZ);    // reverse-Z: near->1, far->0
        r.m[2][3] = -1.0f;                      // w = -view_z
        r.m[3][2] = nearZ * farZ / (farZ - nearZ);
        return r;
    }

    Mat4 perspectiveRhYup(float fovYRadians, float aspect, float nearZ, float farZ) noexcept {
        const float f = 1.0f / std::tan(fovYRadians * 0.5f);
        Mat4 r{};
        r.m[0][0] = f / aspect;
        r.m[1][1] = f;
        r.m[2][2] = farZ / (nearZ - farZ);
        r.m[2][3] = -1.0f;
        r.m[3][2] = nearZ * farZ / (nearZ - farZ);
        return r;
    }

    Mat4 orthographicRhYup(float left, float right, float bottom, float top, float nearZ, float farZ) noexcept {
        Mat4 r{};
        r.m[0][0] =  2.0f / (right - left);
        r.m[1][1] =  2.0f / (top - bottom);
        r.m[2][2] =  1.0f / (nearZ - farZ);
        r.m[3][0] = -(right + left)   / (right - left);
        r.m[3][1] = -(top   + bottom) / (top   - bottom);
        r.m[3][2] =  nearZ / (nearZ - farZ);
        r.m[3][3] =  1.0f;
        return r;
    }

    Mat4 lookAtRh(const Vec3& eye, const Vec3& target, const Vec3& worldUp) noexcept {
        const Vec3 zAxis = normalize(eye - target);                 // camera +Z points backward; objects in front at negative z_view
        const Vec3 xAxis = normalize(cross(worldUp, zAxis));        // camera +X (right)
        const Vec3 yAxis = cross(zAxis, xAxis);                     // camera +Y (up)

        Mat4 r{};
        r.m[0][0] = xAxis.x;  r.m[0][1] = xAxis.y;  r.m[0][2] = xAxis.z;  r.m[0][3] = 0.0f;
        r.m[1][0] = yAxis.x;  r.m[1][1] = yAxis.y;  r.m[1][2] = yAxis.z;  r.m[1][3] = 0.0f;
        r.m[2][0] = zAxis.x;  r.m[2][1] = zAxis.y;  r.m[2][2] = zAxis.z;  r.m[2][3] = 0.0f;
        r.m[3][0] = -dot(xAxis, eye);
        r.m[3][1] = -dot(yAxis, eye);
        r.m[3][2] = -dot(zAxis, eye);
        r.m[3][3] = 1.0f;
        return r;
    }

} // namespace engine::core::math
