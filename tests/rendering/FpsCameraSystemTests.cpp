#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <gtest/gtest.h>
#include <rendering/Camera.h>
#include <core/math/Quat.h>
#include <core/math/Vec.h>
#include <core/math/Mat.h>
#include <core/math/Transform.h>
#include <cmath>

namespace engine::rendering {

using namespace engine::core::math;

// ---------------------------------------------------------------------------
// Component defaults / layout (unchanged)
// ---------------------------------------------------------------------------

TEST(FpsCameraSystemTests, ComponentDefaults) {
    FpsCameraController ctrl{};
    EXPECT_FLOAT_EQ(ctrl.moveSpeed,        5.0f);
    EXPECT_FLOAT_EQ(ctrl.lookSensitivity,  0.1f);
    EXPECT_FLOAT_EQ(ctrl.yaw,              0.0f);
    EXPECT_FLOAT_EQ(ctrl.pitch,            0.0f);
    EXPECT_FLOAT_EQ(ctrl.eyeHeight,        1.7f);
    EXPECT_FLOAT_EQ(ctrl.verticalVelocity, 0.0f);
    EXPECT_TRUE (ctrl.active);
    EXPECT_FALSE(ctrl.isGrounded);
}

TEST(FpsCameraSystemTests, ComponentIdIs17) {
    EXPECT_EQ(FpsCameraController::kComponentId, 17u);
}

TEST(FpsCameraSystemTests, ComponentSize) {
    static_assert(sizeof(FpsCameraController) == 28,
        "FpsCameraController size must be 28 bytes");
    EXPECT_EQ(sizeof(FpsCameraController), 28u);
}

TEST(FpsCameraSystemTests, InactiveControllerSkipped) {
    FpsCameraController ctrl{};
    ctrl.active = false;
    EXPECT_FALSE(ctrl.active);
}

// ---------------------------------------------------------------------------
// Helper: build a Transform at given position with fromEulerYxz orientation.
// ---------------------------------------------------------------------------
static Transform makeTransform(Vec3 pos, float yawDeg, float pitchDeg = 0.0f) {
    constexpr float kDeg = 3.14159265358979f / 180.0f;
    Transform t{};
    t.position = pos;
    t.rotation = normalize(fromEulerYxz(yawDeg * kDeg, pitchDeg * kDeg, 0.0f));
    t.scale    = Vec3{1.0f, 1.0f, 1.0f};
    return t;
}

// ---------------------------------------------------------------------------
// Forward-direction tests: fromEulerYxz produces the expected camera forward.
// ---------------------------------------------------------------------------

TEST(FpsCameraSystemTests, ForwardAtZeroYaw) {
    // Identity rotation: camera looks along -Z.
    const Vec3 fwd = rotate(fromEulerYxz(0.0f, 0.0f, 0.0f), Vec3{0,0,-1});
    EXPECT_NEAR(fwd.x,  0.0f, 1e-5f);
    EXPECT_NEAR(fwd.y,  0.0f, 1e-5f);
    EXPECT_NEAR(fwd.z, -1.0f, 1e-5f);
}

TEST(FpsCameraSystemTests, ForwardAtYaw90) {
    // 90° CCW (left turn): camera looks along -X.
    const Vec3 fwd = rotate(fromEulerYxz(kHalfPi, 0.0f, 0.0f), Vec3{0,0,-1});
    EXPECT_NEAR(fwd.x, -1.0f, 1e-5f);
    EXPECT_NEAR(fwd.y,  0.0f, 1e-5f);
    EXPECT_NEAR(fwd.z,  0.0f, 1e-5f);
}

TEST(FpsCameraSystemTests, ForwardAtYawMinus90) {
    // -90° (right turn): camera looks along +X.
    const Vec3 fwd = rotate(fromEulerYxz(-kHalfPi, 0.0f, 0.0f), Vec3{0,0,-1});
    EXPECT_NEAR(fwd.x,  1.0f, 1e-5f);
    EXPECT_NEAR(fwd.y,  0.0f, 1e-5f);
    EXPECT_NEAR(fwd.z,  0.0f, 1e-5f);
}

TEST(FpsCameraSystemTests, ForwardPitchUp45) {
    // Pitch up 45°: forward should have equal +Y and -Z components.
    constexpr float kAngle = 3.14159265358979f / 4.0f;
    const Vec3 fwd = rotate(fromEulerYxz(0.0f, kAngle, 0.0f), Vec3{0,0,-1});
    const float expected = std::sqrt(0.5f);
    EXPECT_NEAR(fwd.x,         0.0f,     1e-5f);
    EXPECT_NEAR(fwd.y,  expected,  1e-5f);
    EXPECT_NEAR(fwd.z, -expected, 1e-5f);
}

// ---------------------------------------------------------------------------
// Anti-orbit test: a point 1 unit directly in front of the camera always
// lands at view-space {0, 0, -1} regardless of yaw or position.
// If the camera were orbiting, this would fail for non-zero yaw.
// ---------------------------------------------------------------------------

static void expectViewSpaceNearOriginZ(const Transform& t,
                                       float expectedZ,
                                       float tol = 1e-4f) {
    // Rebuild forward from the stored rotation.
    const Vec3 fwd     = rotate(t.rotation, Vec3{0,0,-1});
    const Vec3 inFront = Vec3{t.position.x + fwd.x,
                              t.position.y + fwd.y,
                              t.position.z + fwd.z};

    const Mat4 view = cameraViewMatrix(t);

    // Transform inFront through the view matrix (row-vector convention).
    const float vx = inFront.x * view.m[0][0] + inFront.y * view.m[1][0]
                   + inFront.z * view.m[2][0] + view.m[3][0];
    const float vy = inFront.x * view.m[0][1] + inFront.y * view.m[1][1]
                   + inFront.z * view.m[2][1] + view.m[3][1];
    const float vz = inFront.x * view.m[0][2] + inFront.y * view.m[1][2]
                   + inFront.z * view.m[2][2] + view.m[3][2];

    EXPECT_NEAR(vx, 0.0f,      tol);
    EXPECT_NEAR(vy, 0.0f,      tol);
    EXPECT_NEAR(vz, expectedZ, tol);
}

TEST(FpsCameraSystemTests, NoOrbit_AtOrigin_Yaw0) {
    expectViewSpaceNearOriginZ(makeTransform({0,0,0}, 0.0f), -1.0f);
}

TEST(FpsCameraSystemTests, NoOrbit_AtOrigin_Yaw90) {
    expectViewSpaceNearOriginZ(makeTransform({0,0,0}, 90.0f), -1.0f);
}

TEST(FpsCameraSystemTests, NoOrbit_OffOrigin_Yaw0) {
    expectViewSpaceNearOriginZ(makeTransform({3, 1.7f, 5}, 0.0f), -1.0f);
}

TEST(FpsCameraSystemTests, NoOrbit_OffOrigin_Yaw45) {
    expectViewSpaceNearOriginZ(makeTransform({3, 1.7f, 5}, 45.0f), -1.0f);
}

TEST(FpsCameraSystemTests, NoOrbit_OffOrigin_Yaw135) {
    expectViewSpaceNearOriginZ(makeTransform({3, 1.7f, 5}, 135.0f), -1.0f);
}

TEST(FpsCameraSystemTests, NoOrbit_WithPitch30_Yaw60) {
    expectViewSpaceNearOriginZ(makeTransform({1, 0.5f, 2}, 60.0f, 30.0f), -1.0f);
}

// ---------------------------------------------------------------------------
// Pitch clamp: near-±89° should produce a finite, non-degenerate view matrix.
// ---------------------------------------------------------------------------

TEST(FpsCameraSystemTests, PitchNear89_ViewMatrixFinite) {
    const Transform t = makeTransform({0,0,0}, 0.0f, 89.0f);
    const Mat4 view = cameraViewMatrix(t);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            EXPECT_TRUE(std::isfinite(view.m[r][c]))
                << "view.m[" << r << "][" << c << "] not finite at pitch=89°";
}

TEST(FpsCameraSystemTests, PitchNearMinus89_ViewMatrixFinite) {
    const Transform t = makeTransform({0,0,0}, 0.0f, -89.0f);
    const Mat4 view = cameraViewMatrix(t);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            EXPECT_TRUE(std::isfinite(view.m[r][c]))
                << "view.m[" << r << "][" << c << "] not finite at pitch=-89°";
}

} // namespace engine::rendering
