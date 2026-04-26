// tests/core/math/VecTests.cpp
// Unit tests for Vec2, Vec3, Vec4 (engine/src/core/public/core/math/Vec.h).
// Task #36 — Math unit tests.

#include <core/math/Vec.h>
#include <core/math/Constants.h>
#include <gtest/gtest.h>

using namespace engine::core::math;

// ============================================================
// Vec2 Tests
// ============================================================

TEST(Vec2Test, defaultConstructorIsZero) {
    Vec2 v;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
}

TEST(Vec2Test, constructorSetsComponents) {
    Vec2 v{3.0f, -7.5f};
    EXPECT_FLOAT_EQ(v.x,  3.0f);
    EXPECT_FLOAT_EQ(v.y, -7.5f);
}

TEST(Vec2Test, splatConstructorSetsAllComponents) {
    Vec2 v{4.0f};
    EXPECT_FLOAT_EQ(v.x, 4.0f);
    EXPECT_FLOAT_EQ(v.y, 4.0f);
}

TEST(Vec2Test, addsComponentwise) {
    Vec2 a{1.0f, 2.0f};
    Vec2 b{3.0f, 4.0f};
    Vec2 r = a + b;
    EXPECT_FLOAT_EQ(r.x, 4.0f);
    EXPECT_FLOAT_EQ(r.y, 6.0f);
}

TEST(Vec2Test, subtractsComponentwise) {
    Vec2 a{5.0f, 3.0f};
    Vec2 b{2.0f, 1.0f};
    Vec2 r = a - b;
    EXPECT_FLOAT_EQ(r.x, 3.0f);
    EXPECT_FLOAT_EQ(r.y, 2.0f);
}

TEST(Vec2Test, scalesUniform) {
    Vec2 v{2.0f, -3.0f};
    Vec2 r = v * 4.0f;
    EXPECT_FLOAT_EQ(r.x,  8.0f);
    EXPECT_FLOAT_EQ(r.y, -12.0f);
}

TEST(Vec2Test, scalarMultiplyCommutes) {
    Vec2 v{3.0f, -1.0f};
    Vec2 lhs = v * 5.0f;
    Vec2 rhs = 5.0f * v;
    EXPECT_FLOAT_EQ(lhs.x, rhs.x);
    EXPECT_FLOAT_EQ(lhs.y, rhs.y);
}

TEST(Vec2Test, equalityAndInequality) {
    Vec2 a{1.0f, 2.0f};
    Vec2 b{1.0f, 2.0f};
    Vec2 c{1.0f, 3.0f};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(Vec2Test, negatesComponentwise) {
    Vec2 v{3.0f, -5.0f};
    Vec2 r = -v;
    EXPECT_FLOAT_EQ(r.x, -3.0f);
    EXPECT_FLOAT_EQ(r.y,  5.0f);
}

TEST(Vec2Test, dividesByScalar) {
    Vec2 v{6.0f, -4.0f};
    Vec2 r = v / 2.0f;
    EXPECT_FLOAT_EQ(r.x,  3.0f);
    EXPECT_FLOAT_EQ(r.y, -2.0f);
}

TEST(Vec2Test, compoundAddAssigns) {
    Vec2 v{1.0f, 2.0f};
    v += Vec2{3.0f, 4.0f};
    EXPECT_FLOAT_EQ(v.x, 4.0f);
    EXPECT_FLOAT_EQ(v.y, 6.0f);
}

TEST(Vec2Test, dotProductIsCorrect) {
    Vec2 a{3.0f, 4.0f};
    Vec2 b{1.0f, 2.0f};
    EXPECT_FLOAT_EQ(dot(a, b), 11.0f);  // 3*1 + 4*2
}

// ============================================================
// Vec3 Tests
// ============================================================

TEST(Vec3Test, defaultConstructorIsZero) {
    Vec3 v;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST(Vec3Test, constructorSetsComponents) {
    Vec3 v{1.0f, -2.0f, 3.5f};
    EXPECT_FLOAT_EQ(v.x,  1.0f);
    EXPECT_FLOAT_EQ(v.y, -2.0f);
    EXPECT_FLOAT_EQ(v.z,  3.5f);
}

TEST(Vec3Test, addsComponentwise) {
    Vec3 a{1.0f, 2.0f, 3.0f};
    Vec3 b{4.0f, 5.0f, 6.0f};
    Vec3 r = a + b;
    EXPECT_FLOAT_EQ(r.x, 5.0f);
    EXPECT_FLOAT_EQ(r.y, 7.0f);
    EXPECT_FLOAT_EQ(r.z, 9.0f);
}

TEST(Vec3Test, subtractsComponentwise) {
    Vec3 a{10.0f, 7.0f, 4.0f};
    Vec3 b{3.0f,  2.0f, 1.0f};
    Vec3 r = a - b;
    EXPECT_FLOAT_EQ(r.x, 7.0f);
    EXPECT_FLOAT_EQ(r.y, 5.0f);
    EXPECT_FLOAT_EQ(r.z, 3.0f);
}

TEST(Vec3Test, scalesUniform) {
    Vec3 v{1.0f, -2.0f, 3.0f};
    Vec3 r = v * 2.0f;
    EXPECT_FLOAT_EQ(r.x,  2.0f);
    EXPECT_FLOAT_EQ(r.y, -4.0f);
    EXPECT_FLOAT_EQ(r.z,  6.0f);
}

TEST(Vec3Test, scalarMultiplyCommutes) {
    Vec3 v{1.0f, 2.0f, 3.0f};
    Vec3 lhs = v * 3.0f;
    Vec3 rhs = 3.0f * v;
    EXPECT_FLOAT_EQ(lhs.x, rhs.x);
    EXPECT_FLOAT_EQ(lhs.y, rhs.y);
    EXPECT_FLOAT_EQ(lhs.z, rhs.z);
}

TEST(Vec3Test, negates) {
    Vec3 v{1.0f, -2.0f, 3.0f};
    Vec3 r = -v;
    EXPECT_FLOAT_EQ(r.x, -1.0f);
    EXPECT_FLOAT_EQ(r.y,  2.0f);
    EXPECT_FLOAT_EQ(r.z, -3.0f);
}

TEST(Vec3Test, dotProductIsCorrect) {
    Vec3 a{1.0f, 2.0f, 3.0f};
    Vec3 b{4.0f, 5.0f, 6.0f};
    // 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
    EXPECT_FLOAT_EQ(dot(a, b), 32.0f);
}

TEST(Vec3Test, dotProductOfPerpendicularIsZero) {
    // unitX dot unitY = 0
    EXPECT_FLOAT_EQ(dot(Vec3::unitX(), Vec3::unitY()), 0.0f);
}

TEST(Vec3Test, crossProductIsOrthogonal) {
    Vec3 a{1.0f, 2.0f, 3.0f};
    Vec3 b{4.0f, 5.0f, 6.0f};
    Vec3 c = cross(a, b);
    // cross(a,b) must be perpendicular to both a and b
    EXPECT_NEAR(dot(c, a), 0.0f, 1e-5f);
    EXPECT_NEAR(dot(c, b), 0.0f, 1e-5f);
}

TEST(Vec3Test, crossProductRightHanded) {
    // In a right-handed +Z-forward coordinate system: X cross Y = Z
    Vec3 r = cross(Vec3::unitX(), Vec3::unitY());
    EXPECT_NEAR(r.x, 0.0f, 1e-5f);
    EXPECT_NEAR(r.y, 0.0f, 1e-5f);
    EXPECT_NEAR(r.z, 1.0f, 1e-5f);
}

TEST(Vec3Test, crossProductAntiCommutes) {
    Vec3 a{1.0f, 2.0f, 3.0f};
    Vec3 b{4.0f, 5.0f, 6.0f};
    Vec3 ab = cross(a, b);
    Vec3 ba = cross(b, a);
    EXPECT_NEAR(ab.x, -ba.x, 1e-5f);
    EXPECT_NEAR(ab.y, -ba.y, 1e-5f);
    EXPECT_NEAR(ab.z, -ba.z, 1e-5f);
}

TEST(Vec3Test, lengthSquaredIsCorrect) {
    Vec3 v{3.0f, 4.0f, 0.0f};
    EXPECT_FLOAT_EQ(lengthSquared(v), 25.0f);
}

TEST(Vec3Test, lengthIsCorrect) {
    Vec3 v{3.0f, 4.0f, 0.0f};
    EXPECT_NEAR(length(v), 5.0f, 1e-5f);
}

TEST(Vec3Test, lengthOfUnitVectorsIsOne) {
    EXPECT_NEAR(length(Vec3::unitX()), 1.0f, 1e-5f);
    EXPECT_NEAR(length(Vec3::unitY()), 1.0f, 1e-5f);
    EXPECT_NEAR(length(Vec3::unitZ()), 1.0f, 1e-5f);
}

TEST(Vec3Test, normalizesUnitVector) {
    Vec3 v{3.0f, 4.0f, 0.0f};
    Vec3 n = normalize(v);
    EXPECT_NEAR(length(n), 1.0f, 1e-5f);
    EXPECT_NEAR(n.x, 0.6f, 1e-5f);
    EXPECT_NEAR(n.y, 0.8f, 1e-5f);
    EXPECT_NEAR(n.z, 0.0f, 1e-5f);
}

TEST(Vec3Test, normalizeHandlesZeroLength) {
    // Should return zero vector, not crash or produce NaN
    Vec3 zero;
    Vec3 r = normalize(zero);
    EXPECT_FLOAT_EQ(r.x, 0.0f);
    EXPECT_FLOAT_EQ(r.y, 0.0f);
    EXPECT_FLOAT_EQ(r.z, 0.0f);
}

TEST(Vec3Test, lerpsCorrectly) {
    Vec3 a{0.0f, 0.0f, 0.0f};
    Vec3 b{2.0f, 4.0f, 6.0f};

    Vec3 at0 = lerp(a, b, 0.0f);
    EXPECT_FLOAT_EQ(at0.x, 0.0f);
    EXPECT_FLOAT_EQ(at0.y, 0.0f);
    EXPECT_FLOAT_EQ(at0.z, 0.0f);

    Vec3 at1 = lerp(a, b, 1.0f);
    EXPECT_FLOAT_EQ(at1.x, 2.0f);
    EXPECT_FLOAT_EQ(at1.y, 4.0f);
    EXPECT_FLOAT_EQ(at1.z, 6.0f);

    Vec3 mid = lerp(a, b, 0.5f);
    EXPECT_FLOAT_EQ(mid.x, 1.0f);
    EXPECT_FLOAT_EQ(mid.y, 2.0f);
    EXPECT_FLOAT_EQ(mid.z, 3.0f);
}

TEST(Vec3Test, distanceIsSymmetric) {
    Vec3 a{1.0f, 2.0f, 3.0f};
    Vec3 b{4.0f, 6.0f, 3.0f};
    // distance = sqrt((4-1)^2 + (6-2)^2 + 0) = sqrt(9+16) = 5
    EXPECT_NEAR(distance(a, b), 5.0f, 1e-5f);
    EXPECT_NEAR(distance(b, a), 5.0f, 1e-5f);
    EXPECT_NEAR(distance(a, b), distance(b, a), 1e-5f);
}

TEST(Vec3Test, distanceSquaredIsCorrect) {
    Vec3 a{0.0f, 0.0f, 0.0f};
    Vec3 b{1.0f, 2.0f, 2.0f};
    // 1 + 4 + 4 = 9
    EXPECT_FLOAT_EQ(distanceSquared(a, b), 9.0f);
}

TEST(Vec3Test, reflectAcrossNormal) {
    // Incident vector pointing down-and-forward: (0, -1, 0) reflected across normal (0, 1, 0)
    // should give (0, 1, 0) — reflects upward.
    Vec3 incident{0.0f, -1.0f, 0.0f};
    Vec3 normal  {0.0f,  1.0f, 0.0f};
    Vec3 r = reflect(incident, normal);
    EXPECT_NEAR(r.x, 0.0f, 1e-5f);
    EXPECT_NEAR(r.y, 1.0f, 1e-5f);
    EXPECT_NEAR(r.z, 0.0f, 1e-5f);
}

TEST(Vec3Test, reflectAtFortyFiveDegrees) {
    // Incident (1, -1, 0) reflected across Y normal gives (1, 1, 0)
    Vec3 incident{ 1.0f, -1.0f, 0.0f};
    Vec3 normal  { 0.0f,  1.0f, 0.0f};
    Vec3 r = reflect(incident, normal);
    EXPECT_NEAR(r.x,  1.0f, 1e-5f);
    EXPECT_NEAR(r.y,  1.0f, 1e-5f);
    EXPECT_NEAR(r.z,  0.0f, 1e-5f);
}

TEST(Vec3Test, staticFactoriesAreCorrect) {
    Vec3 z = Vec3::zero();
    EXPECT_FLOAT_EQ(z.x, 0.0f); EXPECT_FLOAT_EQ(z.y, 0.0f); EXPECT_FLOAT_EQ(z.z, 0.0f);

    Vec3 o = Vec3::one();
    EXPECT_FLOAT_EQ(o.x, 1.0f); EXPECT_FLOAT_EQ(o.y, 1.0f); EXPECT_FLOAT_EQ(o.z, 1.0f);

    Vec3 ux = Vec3::unitX();
    EXPECT_FLOAT_EQ(ux.x, 1.0f); EXPECT_FLOAT_EQ(ux.y, 0.0f); EXPECT_FLOAT_EQ(ux.z, 0.0f);

    Vec3 uy = Vec3::unitY();
    EXPECT_FLOAT_EQ(uy.x, 0.0f); EXPECT_FLOAT_EQ(uy.y, 1.0f); EXPECT_FLOAT_EQ(uy.z, 0.0f);

    Vec3 uz = Vec3::unitZ();
    EXPECT_FLOAT_EQ(uz.x, 0.0f); EXPECT_FLOAT_EQ(uz.y, 0.0f); EXPECT_FLOAT_EQ(uz.z, 1.0f);

    Vec3 fwd = Vec3::forward();
    EXPECT_FLOAT_EQ(fwd.x, 0.0f); EXPECT_FLOAT_EQ(fwd.y, 0.0f); EXPECT_FLOAT_EQ(fwd.z, 1.0f);

    Vec3 up = Vec3::up();
    EXPECT_FLOAT_EQ(up.x, 0.0f); EXPECT_FLOAT_EQ(up.y, 1.0f); EXPECT_FLOAT_EQ(up.z, 0.0f);

    Vec3 right = Vec3::right();
    EXPECT_FLOAT_EQ(right.x, 1.0f); EXPECT_FLOAT_EQ(right.y, 0.0f); EXPECT_FLOAT_EQ(right.z, 0.0f);
}

TEST(Vec3Test, minPerComponentIsCorrect) {
    Vec3 a{ 1.0f, 5.0f, -3.0f};
    Vec3 b{ 4.0f, 2.0f,  7.0f};
    Vec3 r = minPerComponent(a, b);
    EXPECT_FLOAT_EQ(r.x,  1.0f);
    EXPECT_FLOAT_EQ(r.y,  2.0f);
    EXPECT_FLOAT_EQ(r.z, -3.0f);
}

TEST(Vec3Test, maxPerComponentIsCorrect) {
    Vec3 a{ 1.0f, 5.0f, -3.0f};
    Vec3 b{ 4.0f, 2.0f,  7.0f};
    Vec3 r = maxPerComponent(a, b);
    EXPECT_FLOAT_EQ(r.x, 4.0f);
    EXPECT_FLOAT_EQ(r.y, 5.0f);
    EXPECT_FLOAT_EQ(r.z, 7.0f);
}

TEST(Vec3Test, equalityAndInequality) {
    Vec3 a{1.0f, 2.0f, 3.0f};
    Vec3 b{1.0f, 2.0f, 3.0f};
    Vec3 c{1.0f, 2.0f, 4.0f};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ============================================================
// Vec4 Tests
// ============================================================

TEST(Vec4Test, defaultConstructorIsZero) {
    Vec4 v;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
    EXPECT_FLOAT_EQ(v.w, 0.0f);
}

TEST(Vec4Test, constructsFromVec3AndW) {
    Vec3 xyz{1.0f, 2.0f, 3.0f};
    Vec4 v{xyz, 4.0f};
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
    EXPECT_FLOAT_EQ(v.w, 4.0f);
}

TEST(Vec4Test, xyzExtractsXYZ) {
    Vec4 v{5.0f, 6.0f, 7.0f, 8.0f};
    Vec3 xyz = v.xyz();
    EXPECT_FLOAT_EQ(xyz.x, 5.0f);
    EXPECT_FLOAT_EQ(xyz.y, 6.0f);
    EXPECT_FLOAT_EQ(xyz.z, 7.0f);
}

TEST(Vec4Test, addsComponentwise) {
    Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};
    Vec4 r = a + b;
    EXPECT_FLOAT_EQ(r.x,  6.0f);
    EXPECT_FLOAT_EQ(r.y,  8.0f);
    EXPECT_FLOAT_EQ(r.z, 10.0f);
    EXPECT_FLOAT_EQ(r.w, 12.0f);
}

TEST(Vec4Test, subtractsComponentwise) {
    Vec4 a{8.0f, 7.0f, 6.0f, 5.0f};
    Vec4 b{1.0f, 2.0f, 3.0f, 4.0f};
    Vec4 r = a - b;
    EXPECT_FLOAT_EQ(r.x, 7.0f);
    EXPECT_FLOAT_EQ(r.y, 5.0f);
    EXPECT_FLOAT_EQ(r.z, 3.0f);
    EXPECT_FLOAT_EQ(r.w, 1.0f);
}

TEST(Vec4Test, dotProductIsCorrect) {
    Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};
    // 1*5 + 2*6 + 3*7 + 4*8 = 5 + 12 + 21 + 32 = 70
    EXPECT_FLOAT_EQ(dot(a, b), 70.0f);
}

TEST(Vec4Test, scalesUniform) {
    Vec4 v{1.0f, -2.0f, 3.0f, -4.0f};
    Vec4 r = v * 2.0f;
    EXPECT_FLOAT_EQ(r.x,  2.0f);
    EXPECT_FLOAT_EQ(r.y, -4.0f);
    EXPECT_FLOAT_EQ(r.z,  6.0f);
    EXPECT_FLOAT_EQ(r.w, -8.0f);
}

TEST(Vec4Test, scalarMultiplyCommutes) {
    Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
    Vec4 lhs = v * 3.0f;
    Vec4 rhs = 3.0f * v;
    EXPECT_FLOAT_EQ(lhs.x, rhs.x);
    EXPECT_FLOAT_EQ(lhs.y, rhs.y);
    EXPECT_FLOAT_EQ(lhs.z, rhs.z);
    EXPECT_FLOAT_EQ(lhs.w, rhs.w);
}

TEST(Vec4Test, equalityAndInequality) {
    Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vec4 b{1.0f, 2.0f, 3.0f, 4.0f};
    Vec4 c{1.0f, 2.0f, 3.0f, 5.0f};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}
