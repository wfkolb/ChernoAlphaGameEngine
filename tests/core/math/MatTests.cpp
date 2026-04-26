// tests/core/math/MatTests.cpp
// Unit tests for Mat4 (engine/src/core/public/core/math/Mat.h).
// Task #36 — Math unit tests.

#include <core/math/Mat.h>
#include <core/math/Vec.h>
#include <core/math/Constants.h>
#include <gtest/gtest.h>
#include <cmath>

using namespace engine::core::math;

static bool nearlyEqualMat4(const Mat4& a, const Mat4& b, float eps = 1e-4f) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (std::abs(a.m[i][j] - b.m[i][j]) > eps) return false;
    return true;
}

// ============================================================
// Mat4 Tests
// ============================================================

TEST(Mat4Test, defaultConstructorIsZero) {
    Mat4 m;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            EXPECT_FLOAT_EQ(m.m[i][j], 0.0f);
}

TEST(Mat4Test, identityIsCorrect) {
    Mat4 id = Mat4::identity();
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            EXPECT_FLOAT_EQ(id.m[i][j], (i == j) ? 1.0f : 0.0f);
}

TEST(Mat4Test, identityMulIsIdentity) {
    Mat4 id = Mat4::identity();
    Mat4 r  = id * id;
    EXPECT_TRUE(nearlyEqualMat4(r, id));
}

TEST(Mat4Test, mulByIdentityIsOriginal) {
    Mat4 a{
        1.0f,  2.0f,  3.0f,  4.0f,
        5.0f,  6.0f,  7.0f,  8.0f,
        9.0f,  10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f,
    };
    EXPECT_TRUE(nearlyEqualMat4(a * Mat4::identity(), a));
    EXPECT_TRUE(nearlyEqualMat4(Mat4::identity() * a, a));
}

TEST(Mat4Test, matMulIsAssociative) {
    Mat4 A{
        1.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 3.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 4.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    Mat4 B = scaling({2.0f, 3.0f, 4.0f});
    Mat4 C = translation({1.0f, -1.0f, 2.0f});
    // (A*B)*C should equal A*(B*C)
    Mat4 lhs = (A * B) * C;
    Mat4 rhs = A * (B * C);
    EXPECT_TRUE(nearlyEqualMat4(lhs, rhs));
}

TEST(Mat4Test, transposeSwapsElements) {
    Mat4 m{
        1.0f,  2.0f,  3.0f,  4.0f,
        5.0f,  6.0f,  7.0f,  8.0f,
        9.0f,  10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f,
    };
    Mat4 t = transpose(m);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            EXPECT_FLOAT_EQ(t.m[i][j], m.m[j][i]);
}

TEST(Mat4Test, transposeOfTransposeIsOriginal) {
    Mat4 m{
        1.0f,  2.0f,  3.0f,  4.0f,
        5.0f,  6.0f,  7.0f,  8.0f,
        9.0f,  10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f,
    };
    EXPECT_TRUE(nearlyEqualMat4(transpose(transpose(m)), m));
}

TEST(Mat4Test, translationMovesPoint) {
    Mat4 t = translation({1.0f, 2.0f, 3.0f});
    Vec3 moved = transformPoint({0.0f, 0.0f, 0.0f}, t);
    EXPECT_NEAR(moved.x, 1.0f, 1e-4f);
    EXPECT_NEAR(moved.y, 2.0f, 1e-4f);
    EXPECT_NEAR(moved.z, 3.0f, 1e-4f);
}

TEST(Mat4Test, translationIsInRow3) {
    // Row-major convention: translation lives in row 3 (m[3][0..2])
    Mat4 t = translation({1.0f, 2.0f, 3.0f});
    EXPECT_NEAR(t.m[3][0], 1.0f, 1e-4f);
    EXPECT_NEAR(t.m[3][1], 2.0f, 1e-4f);
    EXPECT_NEAR(t.m[3][2], 3.0f, 1e-4f);
    EXPECT_NEAR(t.m[3][3], 1.0f, 1e-4f);
}

TEST(Mat4Test, scalingScalesAxes) {
    Mat4 s = scaling({2.0f, 3.0f, 4.0f});
    Vec3 p = transformPoint({1.0f, 1.0f, 1.0f}, s);
    EXPECT_NEAR(p.x, 2.0f, 1e-4f);
    EXPECT_NEAR(p.y, 3.0f, 1e-4f);
    EXPECT_NEAR(p.z, 4.0f, 1e-4f);
}

TEST(Mat4Test, scalingScalesDirection) {
    Mat4 s = scaling({2.0f, 1.0f, 1.0f});
    Vec3 d = transformDirection({1.0f, 0.0f, 0.0f}, s);
    EXPECT_NEAR(d.x, 2.0f, 1e-4f);
    EXPECT_NEAR(d.y, 0.0f, 1e-4f);
    EXPECT_NEAR(d.z, 0.0f, 1e-4f);
}

TEST(Mat4Test, rotationXByHalfPi) {
    // Right-handed: +Y rotated 90° around +X → +Z
    Vec3 r = transformDirection({0.0f, 1.0f, 0.0f}, rotationX(kHalfPi));
    EXPECT_NEAR(r.x, 0.0f, 1e-4f);
    EXPECT_NEAR(r.y, 0.0f, 1e-4f);
    EXPECT_NEAR(r.z, 1.0f, 1e-4f);
}

TEST(Mat4Test, rotationYByHalfPi) {
    // Right-handed: +Z rotated 90° around +Y → +X
    Vec3 r = transformDirection({0.0f, 0.0f, 1.0f}, rotationY(kHalfPi));
    EXPECT_NEAR(r.x, 1.0f, 1e-4f);
    EXPECT_NEAR(r.y, 0.0f, 1e-4f);
    EXPECT_NEAR(r.z, 0.0f, 1e-4f);
}

TEST(Mat4Test, rotationZByHalfPi) {
    // Right-handed: +X rotated 90° around +Z → +Y
    Vec3 r = transformDirection({1.0f, 0.0f, 0.0f}, rotationZ(kHalfPi));
    EXPECT_NEAR(r.x, 0.0f, 1e-4f);
    EXPECT_NEAR(r.y, 1.0f, 1e-4f);
    EXPECT_NEAR(r.z, 0.0f, 1e-4f);
}

TEST(Mat4Test, inverseOfIdentityIsIdentity) {
    Mat4 inv = inverse(Mat4::identity());
    EXPECT_TRUE(nearlyEqualMat4(inv, Mat4::identity()));
}

TEST(Mat4Test, mulByInverseIsIdentity) {
    Mat4 m = translation({3.0f, -1.0f, 2.0f}) * scaling({2.0f, 3.0f, 0.5f});
    Mat4 r = m * inverse(m);
    EXPECT_TRUE(nearlyEqualMat4(r, Mat4::identity()));
}

TEST(Mat4Test, perspectiveReverseZNearMapsToOne) {
    // Reverse-Z: near plane maps to NDC depth 1.0
    Mat4 proj = perspectiveRhYupReverseZ(toRadians(90.0f), 1.0f, 0.1f, 100.0f);
    Vec4 clipPoint = Vec4{0.0f, 0.0f, -0.1f, 1.0f} * proj;
    float ndcZ = clipPoint.z / clipPoint.w;
    EXPECT_NEAR(ndcZ, 1.0f, 1e-4f);
}

TEST(Mat4Test, perspectiveReverseZFarMapsToZero) {
    // Reverse-Z: far plane maps to NDC depth 0.0
    Mat4 proj = perspectiveRhYupReverseZ(toRadians(90.0f), 1.0f, 0.1f, 100.0f);
    Vec4 clipPoint = Vec4{0.0f, 0.0f, -100.0f, 1.0f} * proj;
    float ndcZ = clipPoint.z / clipPoint.w;
    EXPECT_NEAR(ndcZ, 0.0f, 1e-4f);
}

TEST(Mat4Test, lookAtSetsForwardCorrectly) {
    // Camera at origin looking toward +Z; the view matrix transforms
    // world-space +Z into camera-space forward direction (+Z in view space).
    Mat4 view = lookAtRh({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
    Vec3 camForward = transformDirection({0.0f, 0.0f, 1.0f}, view);
    EXPECT_NEAR(camForward.z, 1.0f, 1e-4f);
}
