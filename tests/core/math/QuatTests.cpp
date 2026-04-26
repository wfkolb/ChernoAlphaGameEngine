// tests/core/math/QuatTests.cpp
// Unit tests for Quat (engine/src/core/public/core/math/Quat.h).
// Task #36 — Math unit tests.

#include <core/math/Quat.h>
#include <core/math/Vec.h>
#include <core/math/Mat.h>
#include <core/math/Constants.h>
#include <gtest/gtest.h>
#include <cmath>

using namespace engine::core::math;

static bool nearlyEqualF(float a, float b, float eps = 1e-5f) {
    return std::abs(a - b) <= eps;
}

static bool nearlyEqualQuat(const Quat& a, const Quat& b, float eps = 1e-5f) {
    // Quats q and -q represent the same rotation; check both
    bool same = nearlyEqualF(a.x,  b.x, eps) && nearlyEqualF(a.y,  b.y, eps)
             && nearlyEqualF(a.z,  b.z, eps) && nearlyEqualF(a.w,  b.w, eps);
    bool neg  = nearlyEqualF(a.x, -b.x, eps) && nearlyEqualF(a.y, -b.y, eps)
             && nearlyEqualF(a.z, -b.z, eps) && nearlyEqualF(a.w, -b.w, eps);
    return same || neg;
}

static bool nearlyEqualVec3(const Vec3& a, const Vec3& b, float eps = 1e-5f) {
    return nearlyEqualF(a.x, b.x, eps) && nearlyEqualF(a.y, b.y, eps) && nearlyEqualF(a.z, b.z, eps);
}

// ============================================================
// Quat Tests
// ============================================================

TEST(QuatTest, identityHasUnitLength) {
    Quat id = Quat::identity();
    EXPECT_NEAR(length(id), 1.0f, 1e-5f);
}

TEST(QuatTest, identityIsXYZZeroWOne) {
    Quat id = Quat::identity();
    EXPECT_FLOAT_EQ(id.x, 0.0f);
    EXPECT_FLOAT_EQ(id.y, 0.0f);
    EXPECT_FLOAT_EQ(id.z, 0.0f);
    EXPECT_FLOAT_EQ(id.w, 1.0f);
}

TEST(QuatTest, mulIdentityByIdentityIsIdentity) {
    Quat id = Quat::identity();
    EXPECT_TRUE(nearlyEqualQuat(id * id, id));
}

TEST(QuatTest, conjugateNegatesXYZ) {
    Quat q{0.1f, 0.2f, 0.3f, 0.9274f};  // not normalized, but valid for this test
    Quat c = conjugate(q);
    EXPECT_FLOAT_EQ(c.x, -q.x);
    EXPECT_FLOAT_EQ(c.y, -q.y);
    EXPECT_FLOAT_EQ(c.z, -q.z);
    EXPECT_FLOAT_EQ(c.w,  q.w);
}

TEST(QuatTest, mulByConjugateIsIdentity) {
    Quat q = normalize(fromAxisAngle({1.0f, 0.0f, 0.0f}, 1.2f));
    Quat r = q * conjugate(q);
    EXPECT_TRUE(nearlyEqualQuat(r, Quat::identity()));
}

TEST(QuatTest, fromAxisAngleZeroIsIdentity) {
    Quat q = fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.0f);
    EXPECT_TRUE(nearlyEqualQuat(q, Quat::identity()));
}

TEST(QuatTest, fromAxisAngleHalfPiAroundY) {
    // Rotating +X by +90° around +Y gives -Z (right-handed)
    Quat q = fromAxisAngle({0.0f, 1.0f, 0.0f}, kHalfPi);
    Vec3 result = rotate(q, Vec3::unitX());
    EXPECT_TRUE(nearlyEqualVec3(result, {0.0f, 0.0f, -1.0f}));
}

TEST(QuatTest, normalizeProducesUnitQuat) {
    Quat q{1.0f, 2.0f, 3.0f, 4.0f};  // not unit length
    Quat n = normalize(q);
    EXPECT_NEAR(length(n), 1.0f, 1e-5f);
}

TEST(QuatTest, normalizeHandlesNearZeroInput) {
    // normalize({0,0,0,0}) should return identity, not crash or produce NaN
    Quat zero{0.0f, 0.0f, 0.0f, 0.0f};
    Quat n = normalize(zero);
    EXPECT_TRUE(nearlyEqualQuat(n, Quat::identity()));
}

TEST(QuatTest, slerpAtZeroIsA) {
    Quat a = fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.3f);
    Quat b = fromAxisAngle({0.0f, 1.0f, 0.0f}, 1.1f);
    Quat r = slerp(a, b, 0.0f);
    EXPECT_TRUE(nearlyEqualQuat(r, a));
}

TEST(QuatTest, slerpAtOneIsB) {
    Quat a = fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.3f);
    Quat b = fromAxisAngle({0.0f, 1.0f, 0.0f}, 1.1f);
    Quat r = slerp(a, b, 1.0f);
    // b and -b represent the same rotation
    EXPECT_TRUE(nearlyEqualQuat(r, b));
}

TEST(QuatTest, slerpMidpointIsHalfAngle) {
    // Two quats 90° apart; midpoint slerp should equal 45° rotation
    Quat a   = Quat::identity();
    Quat b   = fromAxisAngle({0.0f, 1.0f, 0.0f}, kHalfPi);
    Quat mid = slerp(a, b, 0.5f);
    Quat expected = fromAxisAngle({0.0f, 1.0f, 0.0f}, kHalfPi * 0.5f);
    // dot > 0.9999 means the angle between them is < ~0.81°
    EXPECT_GT(dot(mid, expected), 0.9999f);
}

TEST(QuatTest, nlerpIsNormalized) {
    Quat a = fromAxisAngle({1.0f, 0.0f, 0.0f}, 0.2f);
    Quat b = fromAxisAngle({0.0f, 1.0f, 0.0f}, kHalfPi);
    Quat n = nlerp(a, b, 0.5f);
    EXPECT_NEAR(length(n), 1.0f, 1e-5f);
}

TEST(QuatTest, toMat4AndFromMat4RoundTrips) {
    Quat q  = fromAxisAngle(normalize(Vec3{1.0f, 2.0f, 3.0f}), 1.23f);
    Mat4 m  = toMat4(q);
    Quat q2 = fromMat4(m);
    EXPECT_TRUE(nearlyEqualQuat(q, q2));
}

TEST(QuatTest, rotateUnitVectors) {
    // identity rotation must leave all unit vectors unchanged
    Quat id = Quat::identity();
    EXPECT_TRUE(nearlyEqualVec3(rotate(id, Vec3::unitX()), Vec3::unitX()));
    EXPECT_TRUE(nearlyEqualVec3(rotate(id, Vec3::unitY()), Vec3::unitY()));
    EXPECT_TRUE(nearlyEqualVec3(rotate(id, Vec3::unitZ()), Vec3::unitZ()));
}
