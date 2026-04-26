// tests/core/math/TransformTests.cpp
// Unit tests for Transform (engine/src/core/public/core/math/Transform.h).
// Task #36 — Math unit tests.

#include <core/math/Transform.h>
#include <core/math/Mat.h>
#include <core/math/Vec.h>
#include <core/math/Quat.h>
#include <core/math/Constants.h>
#include <gtest/gtest.h>
#include <cmath>

using namespace engine::core::math;

static bool nearlyEqualVec3(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
    return std::abs(a.x - b.x) <= eps
        && std::abs(a.y - b.y) <= eps
        && std::abs(a.z - b.z) <= eps;
}

// ============================================================
// Transform Tests
// ============================================================

TEST(TransformTest, defaultTransformHasZeroPosition) {
    Transform t;
    EXPECT_FLOAT_EQ(t.position.x, 0.0f);
    EXPECT_FLOAT_EQ(t.position.y, 0.0f);
    EXPECT_FLOAT_EQ(t.position.z, 0.0f);
}

TEST(TransformTest, defaultTransformHasIdentityRotation) {
    Transform t;
    EXPECT_FLOAT_EQ(t.rotation.x, 0.0f);
    EXPECT_FLOAT_EQ(t.rotation.y, 0.0f);
    EXPECT_FLOAT_EQ(t.rotation.z, 0.0f);
    EXPECT_FLOAT_EQ(t.rotation.w, 1.0f);
}

TEST(TransformTest, defaultTransformHasUnitScale) {
    Transform t;
    EXPECT_FLOAT_EQ(t.scale.x, 1.0f);
    EXPECT_FLOAT_EQ(t.scale.y, 1.0f);
    EXPECT_FLOAT_EQ(t.scale.z, 1.0f);
}

TEST(TransformTest, toMatrixFromIdentityIsIdentity) {
    Transform t;
    Mat4 m = t.toMatrix();
    // Default Transform (no translation, identity rotation, unit scale) must give identity matrix
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            EXPECT_NEAR(m.m[i][j], (i == j) ? 1.0f : 0.0f, 1e-4f);
}

TEST(TransformTest, toMatrixPositionRoundTrips) {
    Transform t;
    t.position = {3.0f, -1.0f, 5.0f};
    Mat4      m    = t.toMatrix();
    Transform back = Transform::fromMatrix(m);
    EXPECT_TRUE(nearlyEqualVec3(back.position, t.position));
}

TEST(TransformTest, toMatrixScaleRoundTrips) {
    Transform t;
    t.scale = {2.0f, 3.0f, 0.5f};
    Transform back = Transform::fromMatrix(t.toMatrix());
    EXPECT_TRUE(nearlyEqualVec3(back.scale, t.scale));
}

TEST(TransformTest, toMatrixRotationRoundTrips) {
    Transform t;
    t.rotation = fromAxisAngle({0.0f, 1.0f, 0.0f}, 1.0f);
    Transform back = Transform::fromMatrix(t.toMatrix());
    // q and -q represent the same rotation; compare the w component magnitude
    bool same = (std::abs(back.rotation.w - t.rotation.w) < 1e-4f);
    bool neg  = (std::abs(back.rotation.w + t.rotation.w) < 1e-4f);
    EXPECT_TRUE(same || neg);
}

TEST(TransformTest, composeIdentities) {
    // compose of two default transforms must equal a default transform
    Transform parent;
    Transform child;
    Transform world = compose(parent, child);
    EXPECT_TRUE(nearlyEqualVec3(world.position, {0.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(nearlyEqualVec3(world.scale,    {1.0f, 1.0f, 1.0f}));
    EXPECT_NEAR(world.rotation.x, 0.0f, 1e-4f);
    EXPECT_NEAR(world.rotation.y, 0.0f, 1e-4f);
    EXPECT_NEAR(world.rotation.z, 0.0f, 1e-4f);
    EXPECT_NEAR(world.rotation.w, 1.0f, 1e-4f);
}

TEST(TransformTest, composeTranslations) {
    Transform parent;
    parent.position = {1.0f, 0.0f, 0.0f};
    Transform child;
    child.position  = {0.0f, 1.0f, 0.0f};
    Transform world = compose(parent, child);
    EXPECT_TRUE(nearlyEqualVec3(world.position, {1.0f, 1.0f, 0.0f}));
}

TEST(TransformTest, composeScales) {
    Transform parent;
    parent.scale = {2.0f, 2.0f, 2.0f};
    Transform child;
    child.scale  = {3.0f, 1.0f, 1.0f};
    Transform world = compose(parent, child);
    EXPECT_TRUE(nearlyEqualVec3(world.scale, {6.0f, 2.0f, 2.0f}));
}
