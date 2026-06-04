#pragma once

#include <physics/ColliderShape.h>
#include <core/math/Vec.h>
#include <core/math/Quat.h>

namespace engine::physics::internal {

struct ContactPoint {
    engine::core::math::Vec3 point;   // world-space contact point
    engine::core::math::Vec3 normal;  // points from body A towards body B
    float depth = 0.0f;               // penetration depth (positive = overlapping)
};

struct ContactManifold {
    ContactPoint contacts[4];
    int          count = 0;
    bool hasContact() const noexcept { return count > 0; }
};

// ── Shape pair tests ─────────────────────────────────────────────────────────

ContactManifold testSphereSphere(const SphereShape& a, const engine::core::math::Vec3& posA,
                                 const SphereShape& b, const engine::core::math::Vec3& posB) noexcept;

ContactManifold testBoxBox(const BoxShape& a,
                           const engine::core::math::Vec3& posA,
                           const engine::core::math::Quat& rotA,
                           const BoxShape& b,
                           const engine::core::math::Vec3& posB,
                           const engine::core::math::Quat& rotB) noexcept;

ContactManifold testSphereBox(const SphereShape& sphere,
                              const engine::core::math::Vec3& spherePos,
                              const BoxShape& box,
                              const engine::core::math::Vec3& boxPos,
                              const engine::core::math::Quat& boxRot) noexcept;

ContactManifold testCapsuleCapsule(const CapsuleShape& a,
                                   const engine::core::math::Vec3& posA,
                                   const engine::core::math::Quat& rotA,
                                   const CapsuleShape& b,
                                   const engine::core::math::Vec3& posB,
                                   const engine::core::math::Quat& rotB) noexcept;

ContactManifold testSphereCapsule(const SphereShape& sphere,
                                  const engine::core::math::Vec3& spherePos,
                                  const CapsuleShape& capsule,
                                  const engine::core::math::Vec3& capsulePos,
                                  const engine::core::math::Quat& capsuleRot) noexcept;

// ── Ray tests ────────────────────────────────────────────────────────────────
// Return hit t (>= 0) along the ray, or -1 on miss.

float rayVsSphere(const engine::core::math::Vec3& origin,
                  const engine::core::math::Vec3& dir,
                  const engine::core::math::Vec3& center,
                  float radius) noexcept;

float rayVsBox(const engine::core::math::Vec3& origin,
               const engine::core::math::Vec3& dir,
               const engine::core::math::Vec3& boxPos,
               const engine::core::math::Quat& boxRot,
               const engine::core::math::Vec3& halfExtents) noexcept;

float rayVsCapsule(const engine::core::math::Vec3& origin,
                   const engine::core::math::Vec3& dir,
                   const engine::core::math::Vec3& capsulePos,
                   const engine::core::math::Quat& capsuleRot,
                   float radius,
                   float halfHeight) noexcept;

// ── Sweep tests ──────────────────────────────────────────────────────────────
// Capsule sweep: returns hit t (>= 0) or -1 on miss.
// The capsule centre axis is vertical (local Y); sweepDir is world-space.

float capsuleSweepVsSphere(const engine::core::math::Vec3& capsuleStart,
                           const engine::core::math::Vec3& sweepDir,
                           float capsuleRadius,
                           float capsuleHalfHeight,
                           const engine::core::math::Vec3& sphereCenter,
                           float sphereRadius) noexcept;

float capsuleSweepVsBox(const engine::core::math::Vec3& capsuleStart,
                        const engine::core::math::Vec3& sweepDir,
                        float capsuleRadius,
                        float capsuleHalfHeight,
                        const engine::core::math::Vec3& boxPos,
                        const engine::core::math::Quat& boxRot,
                        const engine::core::math::Vec3& halfExtents) noexcept;

// ── Overlap ──────────────────────────────────────────────────────────────────

bool sphereOverlapsSphere(const engine::core::math::Vec3& centerA, float radiusA,
                          const engine::core::math::Vec3& centerB, float radiusB) noexcept;

bool sphereOverlapsBox(const engine::core::math::Vec3& sphereCenter, float sphereRadius,
                       const engine::core::math::Vec3& boxPos,
                       const engine::core::math::Quat& boxRot,
                       const engine::core::math::Vec3& halfExtents) noexcept;

// ── Helpers ──────────────────────────────────────────────────────────────────

// Closest point on a line segment [p, q] to point t.
engine::core::math::Vec3 closestPointOnSegment(const engine::core::math::Vec3& p,
                                               const engine::core::math::Vec3& q,
                                               const engine::core::math::Vec3& point) noexcept;

// Closest points between two line segments; returns squared distance.
float closestPointSegmentSegment(const engine::core::math::Vec3& p1,
                                 const engine::core::math::Vec3& q1,
                                 const engine::core::math::Vec3& p2,
                                 const engine::core::math::Vec3& q2,
                                 engine::core::math::Vec3& outC1,
                                 engine::core::math::Vec3& outC2) noexcept;

// Rotate a vector by a unit quaternion.
engine::core::math::Vec3 rotateByQuat(const engine::core::math::Quat& q,
                                      const engine::core::math::Vec3& v) noexcept;

} // namespace engine::physics::internal
