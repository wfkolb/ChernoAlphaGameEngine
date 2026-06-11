#pragma once
#include <core/math/Mat.h>
#include <core/math/Vec.h>
#include <array>
#include <cstdint>

// No DX12 types in this header — shadow resource allocations are handled by
// the internal header src/rendering/internal/ShadowResources.h.

namespace engine::rendering {

// ---------------------------------------------------------------------------
// Shadow map constants
// ---------------------------------------------------------------------------

/// Number of CSM cascades for the directional light shadow.
constexpr uint32_t kShadowCascadeCount    = 4;

/// Depth-buffer resolution for each cascade slice (pixels).
constexpr uint32_t kShadowMapResolution   = 2048;

/// Maximum number of spot/point lights with per-light shadow maps.
constexpr uint32_t kMaxShadowCastingSpots = 8;

/// Default blend ratio between logarithmic and uniform cascade split schemes.
/// 1.0 = pure logarithmic, 0.0 = pure uniform.
/// Override via [render].shadow_split_lambda in engine.toml or SceneGlobals::shadowSplitLambda.
constexpr float    kShadowSplitLambda     = 0.95f;

// ---------------------------------------------------------------------------
// Cascade shadow data (CPU side)
// ---------------------------------------------------------------------------

/// Per-frame output from computeCascades().  Upload to PerFrameConstants on
/// the GPU before the opaque pass each frame.
struct CascadeShadowData {
    /// Light-space view-projection matrix per cascade.
    /// Shadow maps use standard [0,1] depth (NOT reverse-Z).
    core::math::Mat4 viewProj[kShadowCascadeCount];

    /// View-space (positive) linear distances at which each cascade ends.
    /// A pixel whose view-space depth is less than splitDistances[i] is in cascade i.
    float splitDistances[kShadowCascadeCount];
};

// ---------------------------------------------------------------------------
// Cascade split utility
// ---------------------------------------------------------------------------

/// Compute the far-end split distances for each cascade slice using the
/// "practical split scheme" (Nvidia SDK 9.5).
///
/// splitDist[i] = lambda * splitLog[i] + (1 - lambda) * splitUniform[i]
///
/// @param nearZ   Camera near clip distance (positive).
/// @param farZ    Camera far clip distance (positive).
/// @param n       Number of cascades (must match kShadowCascadeCount when used
///                with computeCascades).
/// @param lambda  Blend between uniform (0.0) and logarithmic (1.0) splits.
///                Clamped to [0, 1].
///
/// @returns Array of n positive view-space distances (far end of each cascade).
///          Only the first n elements are meaningful; the rest are zero.
std::array<float, kShadowCascadeCount> computeCascadeSplits(
    float nearZ, float farZ, int n, float lambda) noexcept;

// ---------------------------------------------------------------------------
// Cascade computation
// ---------------------------------------------------------------------------

/// Compute per-cascade light-space view-projection matrices and cascade split
/// distances for a directional shadow caster.
///
/// Implements the "practical split scheme" that blends logarithmic and uniform
/// splits using the supplied lambda, followed by texel-snapping to eliminate
/// shadow shimmer as the camera moves.
///
/// @param cameraView  Camera view matrix (world → view space).
/// @param fovY        Camera vertical field of view in radians.
/// @param aspect      Camera aspect ratio (width / height).
/// @param nearZ       Camera near clip distance (positive, e.g. 0.1f).
/// @param farZ        Camera far clip distance (positive, e.g. 1000.f).
/// @param lightDir    Normalised direction FROM the light source TOWARD the scene
///                    (world space).  Matches the convention used in GpuLight.direction.
/// @param lambda      Split scheme blend factor [0,1].  1.0 = pure log,
///                    0.0 = pure uniform.  Defaults to kShadowSplitLambda.
///
/// @returns CascadeShadowData with viewProj[] and splitDistances[] filled in.
///          Upload viewProj[0..3] to PerFrameConstants.shadowCascadeMat[0..3]
///          and splitDistances[0..3] to PerFrameConstants.cascadeSplits.
CascadeShadowData computeCascades(
    const core::math::Mat4& cameraView,
    float fovY, float aspect, float nearZ, float farZ,
    const core::math::Vec3& lightDir,
    float lambda = kShadowSplitLambda) noexcept;

} // namespace engine::rendering
