#include "NarrowPhase.h"
#include <core/math/Constants.h>
#include <cmath>
#include <algorithm>

namespace engine::physics::internal {

using namespace engine::core::math;

// ── Helper ───────────────────────────────────────────────────────────────────

Vec3 rotateByQuat(const Quat& q, const Vec3& v) noexcept {
    // v' = q * (0,v) * q^-1
    const Vec3 u = {q.x, q.y, q.z};
    const float s = q.w;
    return u * (2.0f * dot(u, v))
         + v  * (s * s - dot(u, u))
         + cross(u, v) * (2.0f * s);
}

Vec3 closestPointOnSegment(const Vec3& p, const Vec3& q, const Vec3& point) noexcept {
    const Vec3 pq = q - p;
    const float len2 = dot(pq, pq);
    if (len2 < kEpsilonNormalSq) return p;
    const float t = dot(point - p, pq) / len2;
    const float tc = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return p + pq * tc;
}

float closestPointSegmentSegment(const Vec3& p1, const Vec3& q1,
                                 const Vec3& p2, const Vec3& q2,
                                 Vec3& outC1, Vec3& outC2) noexcept {
    const Vec3 d1 = q1 - p1;
    const Vec3 d2 = q2 - p2;
    const Vec3 r  = p1 - p2;

    const float a = dot(d1, d1);
    const float e = dot(d2, d2);
    const float f = dot(d2, r);

    float s = 0.0f, t = 0.0f;

    if (a < kEpsilonNormalSq && e < kEpsilonNormalSq) {
        outC1 = p1; outC2 = p2;
        return dot(r, r);
    }
    if (a < kEpsilonNormalSq) {
        s = 0.0f;
        t = f / e;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    } else {
        const float c = dot(d1, r);
        if (e < kEpsilonNormalSq) {
            t = 0.0f;
            s = -c / a;
            s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
        } else {
            const float b    = dot(d1, d2);
            const float denom = a * e - b * b;
            if (denom > kEpsilonNormalSq) {
                s = (b * f - c * e) / denom;
                s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
            } else {
                s = 0.0f;
            }
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = -c / a;
                s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = (b - c) / a;
                s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
            }
        }
    }
    outC1 = p1 + d1 * s;
    outC2 = p2 + d2 * t;
    const Vec3 diff = outC1 - outC2;
    return dot(diff, diff);
}

// ── Sphere vs Sphere ─────────────────────────────────────────────────────────

ContactManifold testSphereSphere(const SphereShape& a, const Vec3& posA,
                                 const SphereShape& b, const Vec3& posB) noexcept {
    ContactManifold m;
    const Vec3 ab    = posB - posA;
    const float dist2 = dot(ab, ab);
    const float rSum  = a.radius + b.radius;
    if (dist2 >= rSum * rSum) return m;

    const float dist = std::sqrt(dist2);
    m.count = 1;
    m.contacts[0].normal = dist > kEpsilonNormalSq ? ab * (1.0f / dist) : Vec3::unitY();
    m.contacts[0].depth  = rSum - dist;
    m.contacts[0].point  = posA + m.contacts[0].normal * (a.radius - m.contacts[0].depth * 0.5f);
    return m;
}

// ── Box vs Box (SAT) ─────────────────────────────────────────────────────────

static float projectBoxOnAxis(const BoxShape& box, const Quat& rot, const Vec3& axis) noexcept {
    const Vec3 rx = rotateByQuat(rot, {1.0f, 0.0f, 0.0f});
    const Vec3 ry = rotateByQuat(rot, {0.0f, 1.0f, 0.0f});
    const Vec3 rz = rotateByQuat(rot, {0.0f, 0.0f, 1.0f});
    return box.halfExtents.x * std::abs(dot(rx, axis))
         + box.halfExtents.y * std::abs(dot(ry, axis))
         + box.halfExtents.z * std::abs(dot(rz, axis));
}

ContactManifold testBoxBox(const BoxShape& a, const Vec3& posA, const Quat& rotA,
                           const BoxShape& b, const Vec3& posB, const Quat& rotB) noexcept {
    ContactManifold m;
    const Vec3 T = posB - posA;

    // Local axes of A and B
    const Vec3 axA[3] = {
        rotateByQuat(rotA, {1,0,0}),
        rotateByQuat(rotA, {0,1,0}),
        rotateByQuat(rotA, {0,0,1}),
    };
    const Vec3 axB[3] = {
        rotateByQuat(rotB, {1,0,0}),
        rotateByQuat(rotB, {0,1,0}),
        rotateByQuat(rotB, {0,0,1}),
    };

    float minOverlap = std::numeric_limits<float>::max();
    Vec3  bestAxis   = Vec3::unitY();

    auto testAxis = [&](const Vec3& axis) -> bool {
        const float axisLen2 = dot(axis, axis);
        if (axisLen2 < kEpsilonNormalSq) return true; // degenerate, skip
        const Vec3 n = axis * (1.0f / std::sqrt(axisLen2));
        const float pA = projectBoxOnAxis(a, rotA, n);
        const float pB = projectBoxOnAxis(b, rotB, n);
        const float d  = std::abs(dot(T, n));
        const float overlap = pA + pB - d;
        if (overlap <= 0.0f) return false; // separating axis found
        if (overlap < minOverlap) {
            minOverlap = overlap;
            bestAxis   = dot(T, n) >= 0.0f ? n : -n;
        }
        return true;
    };

    // 15 SAT axes: 3 face normals of A, 3 face normals of B, 9 edge cross products
    for (int i = 0; i < 3; ++i) { if (!testAxis(axA[i])) return m; }
    for (int i = 0; i < 3; ++i) { if (!testAxis(axB[i])) return m; }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) { if (!testAxis(cross(axA[i], axB[j]))) return m; }

    m.count = 1;
    m.contacts[0].normal = bestAxis;
    m.contacts[0].depth  = minOverlap;
    m.contacts[0].point  = posA + bestAxis * (minOverlap * 0.5f); // approximate
    return m;
}

// ── Sphere vs Box ────────────────────────────────────────────────────────────

ContactManifold testSphereBox(const SphereShape& sphere, const Vec3& spherePos,
                              const BoxShape& box, const Vec3& boxPos, const Quat& boxRot) noexcept {
    ContactManifold m;
    // Transform sphere centre into box local space
    const Quat invRot = {-boxRot.x, -boxRot.y, -boxRot.z, boxRot.w};
    const Vec3 local  = rotateByQuat(invRot, spherePos - boxPos);

    // Clamp to box
    const Vec3& he = box.halfExtents;
    const Vec3 closest = {
        local.x < -he.x ? -he.x : (local.x > he.x ? he.x : local.x),
        local.y < -he.y ? -he.y : (local.y > he.y ? he.y : local.y),
        local.z < -he.z ? -he.z : (local.z > he.z ? he.z : local.z),
    };

    const Vec3 diff  = local - closest;
    const float dist2 = dot(diff, diff);
    if (dist2 >= sphere.radius * sphere.radius) return m;

    const float dist = std::sqrt(dist2);
    m.count = 1;
    // Normal in world space
    const Vec3 localNormal = dist > kEpsilonNormalSq
        ? diff * (1.0f / dist)
        : Vec3::unitY();
    m.contacts[0].normal = rotateByQuat(boxRot, localNormal);
    m.contacts[0].depth  = sphere.radius - dist;
    m.contacts[0].point  = spherePos - m.contacts[0].normal * sphere.radius;
    return m;
}

// ── Capsule vs Capsule ───────────────────────────────────────────────────────

ContactManifold testCapsuleCapsule(const CapsuleShape& a, const Vec3& posA, const Quat& rotA,
                                   const CapsuleShape& b, const Vec3& posB, const Quat& rotB) noexcept {
    ContactManifold m;
    const Vec3 upA = rotateByQuat(rotA, Vec3::unitY());
    const Vec3 upB = rotateByQuat(rotB, Vec3::unitY());
    const Vec3 a0 = posA - upA * a.halfHeight;
    const Vec3 a1 = posA + upA * a.halfHeight;
    const Vec3 b0 = posB - upB * b.halfHeight;
    const Vec3 b1 = posB + upB * b.halfHeight;

    Vec3 c1, c2;
    const float dist2 = closestPointSegmentSegment(a0, a1, b0, b1, c1, c2);
    const float rSum  = a.radius + b.radius;
    if (dist2 >= rSum * rSum) return m;

    const float dist = std::sqrt(dist2);
    m.count = 1;
    const Vec3 dir = dist > kEpsilonNormalSq ? (c2 - c1) * (1.0f / dist) : Vec3::unitY();
    m.contacts[0].normal = dir;
    m.contacts[0].depth  = rSum - dist;
    m.contacts[0].point  = c1 + dir * (a.radius - m.contacts[0].depth * 0.5f);
    return m;
}

// ── Sphere vs Capsule ────────────────────────────────────────────────────────

ContactManifold testSphereCapsule(const SphereShape& sphere, const Vec3& spherePos,
                                  const CapsuleShape& capsule, const Vec3& capsulePos,
                                  const Quat& capsuleRot) noexcept {
    ContactManifold m;
    const Vec3 up  = rotateByQuat(capsuleRot, Vec3::unitY());
    const Vec3 p0  = capsulePos - up * capsule.halfHeight;
    const Vec3 p1  = capsulePos + up * capsule.halfHeight;
    const Vec3 closest = closestPointOnSegment(p0, p1, spherePos);
    const Vec3 diff = spherePos - closest;
    const float dist2 = dot(diff, diff);
    const float rSum  = sphere.radius + capsule.radius;
    if (dist2 >= rSum * rSum) return m;

    const float dist = std::sqrt(dist2);
    m.count = 1;
    m.contacts[0].normal = dist > kEpsilonNormalSq ? diff * (1.0f / dist) : Vec3::unitY();
    m.contacts[0].depth  = rSum - dist;
    m.contacts[0].point  = closest + m.contacts[0].normal * capsule.radius;
    return m;
}

// ── Ray vs Sphere ────────────────────────────────────────────────────────────

float rayVsSphere(const Vec3& origin, const Vec3& dir,
                  const Vec3& center, float radius) noexcept {
    const Vec3 m = origin - center;
    const float b = dot(m, dir);
    const float c = dot(m, m) - radius * radius;
    if (c > 0.0f && b > 0.0f) return -1.0f;
    const float discr = b * b - c;
    if (discr < 0.0f) return -1.0f;
    float t = -b - std::sqrt(discr);
    if (t < 0.0f) t = 0.0f;
    return t;
}

// ── Ray vs Box (OBB) ─────────────────────────────────────────────────────────

float rayVsBox(const Vec3& origin, const Vec3& dir,
               const Vec3& boxPos, const Quat& boxRot,
               const Vec3& halfExtents) noexcept {
    // Transform ray to box local space
    const Quat invRot   = {-boxRot.x, -boxRot.y, -boxRot.z, boxRot.w};
    const Vec3 localOri = rotateByQuat(invRot, origin - boxPos);
    const Vec3 localDir = rotateByQuat(invRot, dir);

    const float* o = &localOri.x;
    const float* d = &localDir.x;
    const float* he = &halfExtents.x;

    float tmin = 0.0f, tmax = std::numeric_limits<float>::max();
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < kEpsilonNormalSq) {
            if (o[i] < -he[i] || o[i] > he[i]) return -1.0f;
        } else {
            float inv = 1.0f / d[i];
            float t1  = (-he[i] - o[i]) * inv;
            float t2  = ( he[i] - o[i]) * inv;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tmin = t1 > tmin ? t1 : tmin;
            tmax = t2 < tmax ? t2 : tmax;
            if (tmin > tmax) return -1.0f;
        }
    }
    return tmin >= 0.0f ? tmin : 0.0f;
}

// ── Ray vs Capsule ───────────────────────────────────────────────────────────

float rayVsCapsule(const Vec3& origin, const Vec3& dir,
                   const Vec3& capsulePos, const Quat& capsuleRot,
                   float radius, float halfHeight) noexcept {
    const Vec3 up = rotateByQuat(capsuleRot, Vec3::unitY());
    const Vec3 pa = capsulePos - up * halfHeight;
    const Vec3 ba = up * (2.0f * halfHeight);

    const Vec3 oa = origin - pa;
    const float baba = dot(ba, ba);
    const float bard = dot(ba, dir);
    const float baoa = dot(ba, oa);
    const float rdoa = dot(dir, oa);
    const float oaoa = dot(oa, oa);

    float a = baba - bard * bard;
    float b = baba * rdoa - baoa * bard;
    float c = baba * oaoa - baoa * baoa - radius * radius * baba;
    float h = b * b - a * c;
    if (h < 0.0f) return -1.0f;
    float t = (-b - std::sqrt(h)) / a;

    float y = baoa + t * bard;
    if (y > 0.0f && y < baba) return t >= 0.0f ? t : -1.0f;

    // Test end caps
    const Vec3 oc = (y <= 0.0f) ? oa : (origin - (pa + ba));
    b = dot(dir, oc);
    c = dot(oc, oc) - radius * radius;
    h = b * b - c;
    if (h < 0.0f) return -1.0f;
    t = -b - std::sqrt(h);
    return t >= 0.0f ? t : -1.0f;
}

// ── Capsule sweep vs Sphere ──────────────────────────────────────────────────

float capsuleSweepVsSphere(const Vec3& capsuleStart, const Vec3& sweepDir,
                           float capsuleRadius, float capsuleHalfHeight,
                           const Vec3& sphereCenter, float sphereRadius) noexcept {
    // Sweep the bottom hemisphere center downward; treat as sphere-sphere sweep
    const float combined = capsuleRadius + sphereRadius;

    // Bottom of the capsule cylinder (the point that leads in sweepDir if sweepDir is downward)
    const float capsuleHalfLen = capsuleHalfHeight;
    // Leading sphere centre along sweep direction
    const Vec3 leadCenter = capsuleStart
        + Vec3::unitY() * (dot(Vec3::unitY(), sweepDir) < 0.0f ? -capsuleHalfLen : capsuleHalfLen);

    // Ray from leadCenter against expanded sphere (radius = combined)
    return rayVsSphere(leadCenter, sweepDir, sphereCenter, combined);
}

// ── Capsule sweep vs Box ─────────────────────────────────────────────────────

float capsuleSweepVsBox(const Vec3& capsuleStart, const Vec3& sweepDir,
                        float capsuleRadius, float capsuleHalfHeight,
                        const Vec3& boxPos, const Quat& boxRot,
                        const Vec3& halfExtents) noexcept {
    // Minkowski sum: expand box by capsuleRadius, sweep capsule centre axis segment.
    // For simplicity sweep both end-sphere centres and take the minimum.
    const Vec3 up   = Vec3::unitY();
    const Vec3 bot  = capsuleStart - up * capsuleHalfHeight;
    const Vec3 top  = capsuleStart + up * capsuleHalfHeight;

    const Vec3 expandedHE = halfExtents + Vec3{capsuleRadius, capsuleRadius, capsuleRadius};

    const float t0 = rayVsBox(bot, sweepDir, boxPos, boxRot, expandedHE);
    const float t1 = rayVsBox(top, sweepDir, boxPos, boxRot, expandedHE);

    if (t0 < 0.0f && t1 < 0.0f) return -1.0f;
    if (t0 < 0.0f) return t1;
    if (t1 < 0.0f) return t0;
    return t0 < t1 ? t0 : t1;
}

// ── Overlap ──────────────────────────────────────────────────────────────────

bool sphereOverlapsSphere(const Vec3& centerA, float radiusA,
                          const Vec3& centerB, float radiusB) noexcept {
    const Vec3 d = centerB - centerA;
    const float rSum = radiusA + radiusB;
    return dot(d, d) < rSum * rSum;
}

bool sphereOverlapsBox(const Vec3& sphereCenter, float sphereRadius,
                       const Vec3& boxPos, const Quat& boxRot,
                       const Vec3& halfExtents) noexcept {
    const Quat invRot = {-boxRot.x, -boxRot.y, -boxRot.z, boxRot.w};
    const Vec3 local  = rotateByQuat(invRot, sphereCenter - boxPos);
    const Vec3& he    = halfExtents;
    const Vec3 closest = {
        local.x < -he.x ? -he.x : (local.x > he.x ? he.x : local.x),
        local.y < -he.y ? -he.y : (local.y > he.y ? he.y : local.y),
        local.z < -he.z ? -he.z : (local.z > he.z ? he.z : local.z),
    };
    const Vec3 diff = local - closest;
    return dot(diff, diff) < sphereRadius * sphereRadius;
}

} // namespace engine::physics::internal
