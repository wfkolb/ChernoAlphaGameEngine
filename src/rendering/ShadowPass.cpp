#include "ShadowPass.h"

#include <core/math/Mat.h>
#include <core/math/Vec.h>

#include <cmath>
#include <cfloat>
#include <array>

namespace engine::rendering {

using core::math::Mat4;
using core::math::Vec3;
using core::math::Vec4;
using core::math::transformPoint;
using core::math::dot;
using core::math::cross;
using core::math::normalize;
using core::math::length;
using core::math::inverse;
using core::math::lookAtRh;
using core::math::orthographicRhYup;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// Compute the 8 world-space corners of the camera frustum slice between
/// viewDepthNear and viewDepthFar (both positive, view-space distances).
///
/// The main renderer uses reverse-Z, but for frustum corner computation we
/// only need world-space geometry, so we reconstruct corners from the view
/// matrix inverse and the per-slice near/far planes.
///
/// halfH = tan(fovY/2) * depth;  halfW = halfH * aspect
std::array<Vec3, 8> frustumCornersWorldSpace(
    const Mat4& invCameraView,
    float fovY, float aspect,
    float nearZ, float farZ) noexcept
{
    // In right-handed Y-up view space, the camera looks along -Z.
    // Frustum corners at a given depth d:
    //   x in [-halfW, +halfW], y in [-halfH, +halfH], z = -d
    float tanHalfFov = std::tan(fovY * 0.5f);

    float halfHNear = tanHalfFov * nearZ;
    float halfWNear = halfHNear  * aspect;
    float halfHFar  = tanHalfFov * farZ;
    float halfWFar  = halfHFar   * aspect;

    // 8 corners in view space (z is negative for in-front objects).
    std::array<Vec3, 8> vsCorners = {{
        // Near plane
        { -halfWNear,  halfHNear, -nearZ },
        {  halfWNear,  halfHNear, -nearZ },
        {  halfWNear, -halfHNear, -nearZ },
        { -halfWNear, -halfHNear, -nearZ },
        // Far plane
        { -halfWFar,   halfHFar,  -farZ  },
        {  halfWFar,   halfHFar,  -farZ  },
        {  halfWFar,  -halfHFar,  -farZ  },
        { -halfWFar,  -halfHFar,  -farZ  },
    }};

    // Transform view-space → world-space using the inverse view matrix.
    std::array<Vec3, 8> wsCorners;
    for (int i = 0; i < 8; ++i) {
        wsCorners[i] = transformPoint(vsCorners[i], invCameraView);
    }
    return wsCorners;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// computeCascadeSplits
// ---------------------------------------------------------------------------

std::array<float, kShadowCascadeCount> computeCascadeSplits(
    float nearZ, float farZ, int n, float lambda) noexcept
{
    // Clamp lambda to [0, 1].
    if (lambda < 0.0f) lambda = 0.0f;
    if (lambda > 1.0f) lambda = 1.0f;
    if (n < 0) n = 0;
    if (n > static_cast<int>(kShadowCascadeCount))
        n = static_cast<int>(kShadowCascadeCount);

    std::array<float, kShadowCascadeCount> result{};
    for (int i = 0; i < n; ++i) {
        float fraction  = static_cast<float>(i + 1) / static_cast<float>(n);
        float splitLog  = nearZ * std::pow(farZ / nearZ, fraction);
        float splitUnif = nearZ + (farZ - nearZ) * fraction;
        result[static_cast<size_t>(i)] = lambda * splitLog + (1.0f - lambda) * splitUnif;
    }
    return result;
}

// ---------------------------------------------------------------------------
// computeCascades
// ---------------------------------------------------------------------------

CascadeShadowData computeCascades(
    const Mat4&  cameraView,
    float        fovY,
    float        aspect,
    float        nearZ,
    float        farZ,
    const Vec3&  lightDir,
    float        lambda) noexcept
{
    CascadeShadowData out{};

    // -----------------------------------------------------------------------
    // Step 1: compute cascade split distances using the practical split scheme.
    // -----------------------------------------------------------------------
    auto splits = computeCascadeSplits(nearZ, farZ,
                                       static_cast<int>(kShadowCascadeCount), lambda);
    float splitDistances[kShadowCascadeCount];
    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        splitDistances[i]      = splits[i];
        out.splitDistances[i]  = splits[i];
    }

    // -----------------------------------------------------------------------
    // Step 2: invert camera view matrix to go view → world.
    // -----------------------------------------------------------------------
    bool invertOk = false;
    Mat4 invCameraView = core::math::inverse(cameraView, &invertOk);
    if (!invertOk) {
        // Degenerate camera — return identity matrices.
        for (uint32_t i = 0; i < kShadowCascadeCount; ++i)
            out.viewProj[i] = Mat4::identity();
        return out;
    }

    // -----------------------------------------------------------------------
    // Step 3: for each cascade, build the light-space view-projection matrix.
    // -----------------------------------------------------------------------
    // Light view matrix: look from behind the scene along lightDir.
    // lightDir points FROM light TOWARD scene; we want the eye behind the scene
    // so that objects in the scene are in front of the camera.
    // We place the eye at the world origin temporarily — it will be displaced
    // per-cascade after computing the frustum centroid.

    // Build a stable up vector (avoid parallel with lightDir).
    Vec3 up = Vec3::unitY();
    if (std::abs(dot(up, lightDir)) > 0.99f)
        up = Vec3::unitZ();

    float cascadeNear = nearZ;

    for (uint32_t c = 0; c < kShadowCascadeCount; ++c) {
        float cascadeFar = splitDistances[c];

        // ----------------------------------------------------------------
        // 3a. Frustum corners for this cascade slice in world space.
        // ----------------------------------------------------------------
        auto wsCorners = frustumCornersWorldSpace(
            invCameraView, fovY, aspect, cascadeNear, cascadeFar);

        // Centroid of the 8 corners (used to position the light camera).
        Vec3 centroid{ 0.0f, 0.0f, 0.0f };
        for (const auto& c2 : wsCorners)
            centroid = centroid + c2;
        centroid = centroid * (1.0f / 8.0f);

        // ----------------------------------------------------------------
        // 3b. Build a temporary light view matrix centred on the frustum.
        // ----------------------------------------------------------------
        // eye = centroid - lightDir * someOffset;  lightDir points to scene,
        // so the eye is placed BEHIND the scene along lightDir.
        // The offset just needs to be large enough to include the scene;
        // we compute it from the frustum radius below.
        Vec3 lightEye = centroid - lightDir;  // placeholder; adjusted after radius

        Mat4 lightView = lookAtRh(lightEye, centroid, up);

        // ----------------------------------------------------------------
        // 3c. Transform all 8 corners into light space and find AABB.
        // ----------------------------------------------------------------
        float minX =  FLT_MAX, maxX = -FLT_MAX;
        float minY =  FLT_MAX, maxY = -FLT_MAX;
        float minZ =  FLT_MAX, maxZ = -FLT_MAX;

        for (const auto& wc : wsCorners) {
            Vec3 lc = transformPoint(wc, lightView);
            if (lc.x < minX) minX = lc.x;
            if (lc.x > maxX) maxX = lc.x;
            if (lc.y < minY) minY = lc.y;
            if (lc.y > maxY) maxY = lc.y;
            if (lc.z < minZ) minZ = lc.z;
            if (lc.z > maxZ) maxZ = lc.z;
        }

        // Pull the near plane back to capture shadow casters outside the
        // cascade slice that still cast into it (geometry behind the camera).
        // A scene-specific multiplier of 10 is a safe default.
        constexpr float kNearPullback = 10.0f;
        if (minZ < 0.0f)
            minZ *= kNearPullback;
        else
            minZ /= kNearPullback;

        // ----------------------------------------------------------------
        // 3d. Texel snapping — eliminate shimmer as the camera moves.
        // ----------------------------------------------------------------
        // The frustum radius (half-extent in X or Y, taking the larger) gives
        // us the texel size in light space.
        float frustumRadius = std::max(maxX - minX, maxY - minY) * 0.5f;
        float texelSize     = (2.0f * frustumRadius) /
                              static_cast<float>(kShadowMapResolution);

        // Centroid in light space (recompute with the placeholder lightView).
        Vec3 lightCentroid = transformPoint(centroid, lightView);

        // Snap X/Y of the light-space centroid to the nearest texel.
        float snappedX = std::floor(lightCentroid.x / texelSize) * texelSize;
        float snappedY = std::floor(lightCentroid.y / texelSize) * texelSize;

        // The offset between snapped and un-snapped centroid is applied to the
        // world-space eye position.  Because our basis vectors (right/up in light
        // space) come from the lookAtRh call, we extract them from lightView rows.
        // In our row-major convention, row i of M holds the i-th basis row,
        // and the view matrix stores right/up/forward as its first 3 rows.
        Vec3 lightRight   = normalize({ lightView.m[0][0], lightView.m[1][0], lightView.m[2][0] });
        Vec3 lightUp      = normalize({ lightView.m[0][1], lightView.m[1][1], lightView.m[2][1] });

        float dX = snappedX - lightCentroid.x;
        float dY = snappedY - lightCentroid.y;

        // Recompute the light eye with the snapped offset.
        Vec3 snappedEye = lightEye
                        + lightRight * dX
                        + lightUp    * dY;

        // Build the final snapped light view matrix.
        Mat4 snappedLightView = lookAtRh(snappedEye, snappedEye + lightDir, up);

        // ----------------------------------------------------------------
        // 3e. Build orthographic projection.
        // Shadow maps use standard [0,1] depth (NOT reverse-Z): use
        // orthographicRhYup which maps nearZ -> 0, farZ -> 1.
        // ----------------------------------------------------------------
        // Re-snap the AABB extents symmetrically around the snapped centroid.
        float halfExt = frustumRadius;
        Mat4 lightProj = orthographicRhYup(
            -halfExt,  halfExt,    // left, right
            -halfExt,  halfExt,    // bottom, top
             minZ,     maxZ);      // near, far (standard depth)

        out.viewProj[c] = snappedLightView * lightProj;

        // Advance near for next cascade.
        cascadeNear = cascadeFar;
    }

    return out;
}

} // namespace engine::rendering
