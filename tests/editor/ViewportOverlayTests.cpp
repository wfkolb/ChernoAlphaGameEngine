// Tests for viewport icon overlay helpers — specifically the worldToScreen projection.
// No GPU or ImGui window is needed; these test the math directly.

#include <gtest/gtest.h>

#include <core/math/Mat.h>
#include <core/math/Vec.h>

using engine::core::math::Vec3;
using engine::core::math::Vec4;
using engine::core::math::Mat4;

// Replicate the worldToScreen logic from ViewportPanel.cpp for unit testing.
// Vec4(Vec3, float) constructor is confirmed available in Vec.h.
static bool worldToScreenTest(const Vec3& world, const Mat4& viewProj,
                               float originX, float originY, float w, float h,
                               float& outX, float& outY) {
    Vec4 clip = Vec4{world, 1.0f} * viewProj;
    if (clip.w <= 0.0001f) return false;
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    outX = originX + (ndcX * 0.5f + 0.5f) * w;
    outY = originY + (1.0f - (ndcY * 0.5f + 0.5f)) * h;
    return true;
}

// Helper: build an identity Mat4.
static Mat4 identityMat4() {
    Mat4 m{};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
    return m;
}

// A point at NDC (0,0) with an identity viewProj should land at the viewport centre.
TEST(ViewportOverlay, OriginProjectsToCenter) {
    const Mat4 identity = identityMat4();

    float x, y;
    bool ok = worldToScreenTest({0.0f, 0.0f, 0.0f}, identity,
                                0.0f, 0.0f, 800.0f, 600.0f,
                                x, y);
    EXPECT_TRUE(ok);
    EXPECT_NEAR(x, 400.0f, 0.5f);
    EXPECT_NEAR(y, 300.0f, 0.5f);
}

// A point with clip.w <= 0 is behind the camera and must return false.
TEST(ViewportOverlay, BehindCameraReturnsFalse) {
    // A zero matrix produces clip = {0,0,0,0}, so clip.w == 0 → behind camera.
    Mat4 zero{};
    float x, y;
    bool ok = worldToScreenTest({1.0f, 1.0f, 1.0f}, zero,
                                0.0f, 0.0f, 800.0f, 600.0f,
                                x, y);
    EXPECT_FALSE(ok);
}

// A non-zero viewport origin shifts the output pixel coordinates by that offset.
TEST(ViewportOverlay, OffsetOriginShiftsOutput) {
    const Mat4 identity = identityMat4();

    float x, y;
    bool ok = worldToScreenTest({0.0f, 0.0f, 0.0f}, identity,
                                100.0f, 50.0f, 800.0f, 600.0f,
                                x, y);
    EXPECT_TRUE(ok);
    EXPECT_NEAR(x, 500.0f, 0.5f);   // 100 + (0.5 * 800)
    EXPECT_NEAR(y, 350.0f, 0.5f);   // 50  + (0.5 * 600)
}

// A point translated to NDC (+1, 0) — fully right — should map to (width, height/2).
TEST(ViewportOverlay, RightEdgeNDCMapsToRightEdgePixel) {
    // Build a matrix that maps {1,0,0,1} → clip {1,0,0,1} (NDC x=1).
    const Mat4 identity = identityMat4();

    float x, y;
    // NDC (1, 0) → pixel (800, 300) in a 800×600 viewport with no origin offset.
    // With identity VP, world (1,0,0) → clip (1,0,0,1) → NDC (1,0).
    bool ok = worldToScreenTest({1.0f, 0.0f, 0.0f}, identity,
                                0.0f, 0.0f, 800.0f, 600.0f,
                                x, y);
    EXPECT_TRUE(ok);
    EXPECT_NEAR(x, 800.0f, 0.5f);
    EXPECT_NEAR(y, 300.0f, 0.5f);
}

// Y-axis: NDC +1 in Y (top of screen) maps to pixel y=0 (top of viewport).
TEST(ViewportOverlay, TopNDCMapsToYZeroPixel) {
    const Mat4 identity = identityMat4();

    float x, y;
    // World (0,1,0) → clip (0,1,0,1) → NDC y=1 → pixel y = 0 + (1 - (0.5+0.5))*600 = 0.
    bool ok = worldToScreenTest({0.0f, 1.0f, 0.0f}, identity,
                                0.0f, 0.0f, 800.0f, 600.0f,
                                x, y);
    EXPECT_TRUE(ok);
    EXPECT_NEAR(x, 400.0f, 0.5f);
    EXPECT_NEAR(y,   0.0f, 0.5f);
}
