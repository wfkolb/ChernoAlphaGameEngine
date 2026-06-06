#define NOMINMAX
#include "physics/PhysicsWorld.h"
#include "BroadPhase.h"
#include "NarrowPhase.h"
#include "CharacterControllerImpl.h"
#include <core/log.h>
#include <core/math/Constants.h>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>

namespace engine::physics {

using namespace engine::core::math;
using namespace engine::core::ecs;

// ── Internal body data ────────────────────────────────────────────────────────

struct BodyData {
    Entity    entity;
    Vec3      position;
    Quat      rotation;
    Vec3      linearVelocity  = Vec3::zero();
    Vec3      angularVelocity = Vec3::zero();
    Vec3      accumulatedForce = Vec3::zero();
    RigidBody rb;
    Collider  collider;
    CharacterController cc;
    bool      isCharacterController = false;
    bool      isGrounded            = false;
    uint32_t  id                    = 0;
};

// ── Impl ──────────────────────────────────────────────────────────────────────

struct PhysicsWorld::Impl {
    Vec3 gravity = {0.0f, -9.81f, 0.0f};

    std::vector<BodyData>            bodies;
    std::unordered_map<uint32_t, size_t> idToIndex;
    uint32_t nextId = 1u;

    internal::StaticBVH  staticBVH;
    internal::DynamicGrid dynamicGrid;
    bool staticDirty = true;

    BodyData* find(BodyId id) noexcept {
        auto it = idToIndex.find(id);
        return it != idToIndex.end() ? &bodies[it->second] : nullptr;
    }
    const BodyData* find(BodyId id) const noexcept {
        auto it = idToIndex.find(id);
        return it != idToIndex.end() ? &bodies[it->second] : nullptr;
    }

    uint32_t allocId() noexcept { return nextId++; }

    void remove(BodyId id) {
        auto it = idToIndex.find(id);
        if (it == idToIndex.end()) return;
        const size_t idx  = it->second;
        const size_t last = bodies.size() - 1u;
        if (idx != last) {
            bodies[idx] = std::move(bodies[last]);
            idToIndex[bodies[idx].id] = idx;
        }
        bodies.pop_back();
        idToIndex.erase(id);
        staticDirty = true;
    }

    void rebuildStaticBVH() {
        std::vector<internal::BroadEntry> entries;
        for (size_t i = 0; i < bodies.size(); ++i) {
            const BodyData& b = bodies[i];
            if (b.isCharacterController) continue;
            if (b.rb.type == RigidBodyType::Static) {
                internal::BroadEntry e;
                e.bodyIndex = static_cast<uint32_t>(i);
                e.aabb      = internal::computeAABB(b.collider, b.position, b.rotation);
                entries.push_back(e);
            }
        }
        staticBVH.build(entries);
        staticDirty = false;
    }

    void rebuildDynamicGrid() {
        dynamicGrid.clear();
        for (size_t i = 0; i < bodies.size(); ++i) {
            const BodyData& b = bodies[i];
            if (b.isCharacterController) continue;
            if (b.rb.type != RigidBodyType::Static) {
                dynamicGrid.insert(static_cast<uint32_t>(i),
                    internal::computeAABB(b.collider, b.position, b.rotation));
            }
        }
    }
};

// ── Constructor / Destructor ──────────────────────────────────────────────────

PhysicsWorld::PhysicsWorld()  : impl_(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() = default;

// ── Gravity ───────────────────────────────────────────────────────────────────

void     PhysicsWorld::setGravity(Vec3 g) noexcept { impl_->gravity = g; }
Vec3     PhysicsWorld::getGravity()      const noexcept { return impl_->gravity; }

// ── Body management ───────────────────────────────────────────────────────────

BodyId PhysicsWorld::addRigidBody(Entity entity,
                                   const engine::core::Transform& transform,
                                   const RigidBody& rb,
                                   const Collider& collider) {
    BodyData b;
    b.entity   = entity;
    b.position = transform.position;
    b.rotation = transform.rotation;
    b.rb       = rb;
    b.linearVelocity  = rb.velocity;
    b.angularVelocity = rb.angularVelocity;
    b.collider = collider;
    b.id       = impl_->allocId();

    const size_t idx = impl_->bodies.size();
    impl_->bodies.push_back(std::move(b));
    impl_->idToIndex[impl_->bodies[idx].id] = idx;
    impl_->staticDirty = true;
    return impl_->bodies[idx].id;
}

void PhysicsWorld::removeBody(BodyId id) {
    impl_->remove(id);
}

engine::core::Transform PhysicsWorld::getTransform(BodyId id) const {
    const BodyData* b = impl_->find(id);
    if (!b) return {};
    engine::core::Transform t;
    t.position = b->position;
    t.rotation = b->rotation;
    return t;
}

void PhysicsWorld::setTransform(BodyId id, const engine::core::Transform& t) {
    BodyData* b = impl_->find(id);
    if (!b) return;
    b->position = t.position;
    b->rotation = t.rotation;
    if (b->rb.type == RigidBodyType::Static) impl_->staticDirty = true;
}

void PhysicsWorld::setTransformByEntity(Entity entity,
                                         const engine::core::Transform& t) {
    for (BodyData& b : impl_->bodies) {
        if (b.entity != entity) continue;
        b.position = t.position;
        b.rotation = t.rotation;
        if (b.rb.type == RigidBodyType::Static) impl_->staticDirty = true;
    }
}

void PhysicsWorld::setLinearVelocity(BodyId id, Vec3 v) {
    BodyData* b = impl_->find(id);
    if (b) b->linearVelocity = v;
}

Vec3 PhysicsWorld::getLinearVelocity(BodyId id) const {
    const BodyData* b = impl_->find(id);
    return b ? b->linearVelocity : Vec3::zero();
}

void PhysicsWorld::applyForce(BodyId id, Vec3 force) {
    BodyData* b = impl_->find(id);
    if (b && b->rb.type == RigidBodyType::Dynamic) b->accumulatedForce += force;
}

void PhysicsWorld::applyImpulse(BodyId id, Vec3 impulse) {
    BodyData* b = impl_->find(id);
    if (b && b->rb.type == RigidBodyType::Dynamic && b->rb.mass > 0.0f)
        b->linearVelocity += impulse * (1.0f / b->rb.mass);
}

// ── Character controller management ──────────────────────────────────────────

BodyId PhysicsWorld::addCharacterController(Entity entity,
                                             const engine::core::Transform& transform,
                                             const CharacterController& cc) {
    BodyData b;
    b.entity   = entity;
    b.position = transform.position;
    b.rotation = transform.rotation;
    b.isCharacterController = true;
    b.cc       = cc;
    b.id       = impl_->allocId();

    // Build a capsule collider matching the CC dimensions
    Collider col;
    CapsuleShape cap;
    cap.radius     = cc.capsuleRadius;
    cap.halfHeight = (cc.capsuleHeight * 0.5f) - cc.capsuleRadius;
    col.shape      = cap;
    b.collider     = col;

    const size_t idx = impl_->bodies.size();
    impl_->bodies.push_back(std::move(b));
    impl_->idToIndex[impl_->bodies[idx].id] = idx;
    return impl_->bodies[idx].id;
}

void PhysicsWorld::setDesiredVelocity(BodyId id, Vec3 v) {
    BodyData* b = impl_->find(id);
    if (b && b->isCharacterController) b->cc.desiredVelocity = v;
}

bool PhysicsWorld::isGrounded(BodyId id) const {
    const BodyData* b = impl_->find(id);
    return b && b->isCharacterController && b->isGrounded;
}

// ── Step ──────────────────────────────────────────────────────────────────────

void PhysicsWorld::step(float dt) {
    if (impl_->staticDirty) impl_->rebuildStaticBVH();
    impl_->rebuildDynamicGrid();

    const float invDt = dt > kEpsilonNormalSq ? 1.0f / dt : 0.0f;
    (void)invDt;

    // 1. Integrate forces and velocities for dynamic bodies (semi-implicit Euler)
    for (BodyData& b : impl_->bodies) {
        if (b.isCharacterController || b.rb.type != RigidBodyType::Dynamic) continue;
        if (b.rb.mass <= 0.0f) continue;

        const float invMass = 1.0f / b.rb.mass;
        Vec3 accel = impl_->gravity + b.accumulatedForce * invMass;

        // Apply freeze flags
        if (b.rb.freezeFlags & kFreezePosX) { accel.x = 0.0f; b.linearVelocity.x = 0.0f; }
        if (b.rb.freezeFlags & kFreezePosY) { accel.y = 0.0f; b.linearVelocity.y = 0.0f; }
        if (b.rb.freezeFlags & kFreezePosZ) { accel.z = 0.0f; b.linearVelocity.z = 0.0f; }

        b.linearVelocity += accel * dt;
        b.linearVelocity *= std::pow(1.0f - b.rb.linearDamping, dt);
        b.accumulatedForce = Vec3::zero();
    }

    // 2. Collision detection & resolution for dynamic bodies vs static bodies
    struct ContactInfo {
        size_t  dynIdx;
        Vec3    normal;
        float   depth;
    };
    std::vector<ContactInfo> contacts;

    for (size_t di = 0; di < impl_->bodies.size(); ++di) {
        BodyData& dyn = impl_->bodies[di];
        if (dyn.isCharacterController || dyn.rb.type != RigidBodyType::Dynamic) continue;

        const AABB dynAABB = internal::computeAABB(dyn.collider, dyn.position, dyn.rotation);

        // Query static BVH
        std::vector<uint32_t> candidates;
        impl_->staticBVH.query(dynAABB, candidates);

        for (uint32_t si : candidates) {
            BodyData& stat = impl_->bodies[si];
            internal::ContactManifold manifold;

            // Dispatch narrow phase based on shape types
            auto narrow = [&]() {
                return std::visit([&](const auto& shA) -> internal::ContactManifold {
                    return std::visit([&](const auto& shB) -> internal::ContactManifold {
                        using A = std::decay_t<decltype(shA)>;
                        using B = std::decay_t<decltype(shB)>;

                        if constexpr (std::is_same_v<A, SphereShape> && std::is_same_v<B, SphereShape>)
                            return internal::testSphereSphere(shA, dyn.position, shB, stat.position);
                        else if constexpr (std::is_same_v<A, SphereShape> && std::is_same_v<B, BoxShape>)
                            return internal::testSphereBox(shA, dyn.position, shB, stat.position, stat.rotation);
                        else if constexpr (std::is_same_v<A, BoxShape> && std::is_same_v<B, BoxShape>)
                            return internal::testBoxBox(shA, dyn.position, dyn.rotation, shB, stat.position, stat.rotation);
                        else if constexpr (std::is_same_v<A, BoxShape> && std::is_same_v<B, SphereShape>) {
                            auto m = internal::testSphereBox(shB, stat.position, shA, dyn.position, dyn.rotation);
                            for (int c = 0; c < m.count; ++c) m.contacts[c].normal = -m.contacts[c].normal;
                            return m;
                        }
                        else if constexpr (std::is_same_v<A, CapsuleShape> && std::is_same_v<B, CapsuleShape>)
                            return internal::testCapsuleCapsule(shA, dyn.position, dyn.rotation, shB, stat.position, stat.rotation);
                        else if constexpr (std::is_same_v<A, SphereShape> && std::is_same_v<B, CapsuleShape>)
                            return internal::testSphereCapsule(shA, dyn.position, shB, stat.position, stat.rotation);
                        else {
                            internal::ContactManifold empty;
                            return empty;
                        }
                    }, stat.collider.shape);
                }, dyn.collider.shape);
            };

            manifold = narrow();
            for (int c = 0; c < manifold.count; ++c) {
                if (manifold.contacts[c].depth > 0.0f)
                    contacts.push_back({di, manifold.contacts[c].normal, manifold.contacts[c].depth});
            }
        }
    }

    // 3. Sequential impulse solver (single iteration for Phase 7)
    for (const ContactInfo& ci : contacts) {
        BodyData& b = impl_->bodies[ci.dynIdx];
        if (b.rb.type != RigidBodyType::Dynamic) continue;

        const float vn = dot(b.linearVelocity, ci.normal);
        if (vn >= 0.0f) continue; // already separating

        const float invMass = b.rb.mass > 0.0f ? 1.0f / b.rb.mass : 0.0f;
        const float j       = -(1.0f + b.rb.restitution) * vn * invMass;
        b.linearVelocity += ci.normal * j;

        // Baumgarte position correction
        constexpr float kBaumgarte    = 0.2f;
        constexpr float kSlop         = 0.005f;
        const float correction = kBaumgarte * std::max(ci.depth - kSlop, 0.0f) * invMass;
        b.position += ci.normal * correction;
    }

    // 4. Integrate positions for dynamic bodies
    for (BodyData& b : impl_->bodies) {
        if (b.isCharacterController || b.rb.type != RigidBodyType::Dynamic) continue;
        b.position += b.linearVelocity * dt;
    }

    // 5. Update character controllers
    for (BodyData& b : impl_->bodies) {
        if (!b.isCharacterController) continue;

        const float capsuleRadius     = b.cc.capsuleRadius;
        const float capsuleHalfHeight = internal::capsuleCylinderHalfHeight(b.cc);
        const Vec3  desiredVel        = internal::clampGroundedVelocity(b.cc.desiredVelocity, b.isGrounded);
        const Vec3  displacement      = desiredVel * dt;
        const float dispLen           = length(displacement);

        if (dispLen > kEpsilonNormalSq) {
            const Vec3 dir  = displacement * (1.0f / dispLen);
            const RaycastHit hit = sweepCapsule(b.position, dir, capsuleRadius, capsuleHalfHeight, dispLen);
            if (hit.hasHit) {
                constexpr float kSkin = 0.001f;
                const float safeT = hit.distance > kSkin ? hit.distance - kSkin : 0.0f;
                b.position += dir * safeT;
                const Vec3 remaining = internal::slideAlongSurface(displacement - dir * safeT, hit.normal);
                b.position += remaining;
            } else {
                b.position += displacement;
            }
        }

        // Ground check: sweep down a small amount
        constexpr float kGroundCheckDist = 0.12f;
        const float cosMax = std::cos(b.cc.maxSlopeAngle * kDegToRad);
        const RaycastHit ground = sweepCapsule(b.position, {0,-1,0}, capsuleRadius, capsuleHalfHeight, kGroundCheckDist);
        const bool groundedThisFrame = ground.hasHit && ground.normal.y >= cosMax;
        b.isGrounded = groundedThisFrame;
        internal::updateGroundedState(b.cc, groundedThisFrame, dt);
    }
}

// ── Queries ───────────────────────────────────────────────────────────────────

RaycastHit PhysicsWorld::raycast(const Vec3& origin, const Vec3& dir,
                                  float maxDist, const QueryFilter& filter) const {
    RaycastHit best;
    best.distance = maxDist;

    for (const BodyData& b : impl_->bodies) {
        if (!filter.collides(b.collider.layerIndex, b.collider.layerIndex)) continue;
        if (b.isCharacterController) continue;

        float t = std::visit([&](const auto& shape) -> float {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, SphereShape>)
                return internal::rayVsSphere(origin, dir, b.position, shape.radius);
            else if constexpr (std::is_same_v<T, BoxShape>)
                return internal::rayVsBox(origin, dir, b.position, b.rotation, shape.halfExtents);
            else if constexpr (std::is_same_v<T, CapsuleShape>)
                return internal::rayVsCapsule(origin, dir, b.position, b.rotation, shape.radius, shape.halfHeight);
            else return -1.0f;
        }, b.collider.shape);

        if (t >= 0.0f && t < best.distance) {
            best.hasHit   = true;
            best.distance = t;
            best.entity   = b.entity;
            best.point    = origin + dir * t;
            // Approximate normal: sphere
            std::visit([&](const auto& shape) {
                using T = std::decay_t<decltype(shape)>;
                if constexpr (std::is_same_v<T, SphereShape>) {
                    best.normal = normalize(best.point - b.position);
                } else {
                    best.normal = Vec3::unitY();
                }
            }, b.collider.shape);
        }
    }
    return best;
}

RaycastHit PhysicsWorld::sweepCapsule(const Vec3& origin, const Vec3& dir,
                                       float capsuleRadius, float capsuleHalfHeight,
                                       float maxDist, const QueryFilter& filter) const {
    RaycastHit best;
    best.distance = maxDist;

    for (const BodyData& b : impl_->bodies) {
        if (!filter.collides(b.collider.layerIndex, b.collider.layerIndex)) continue;
        if (b.isCharacterController) continue;

        float t = std::visit([&](const auto& shape) -> float {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, SphereShape>)
                return internal::capsuleSweepVsSphere(origin, dir, capsuleRadius, capsuleHalfHeight, b.position, shape.radius);
            else if constexpr (std::is_same_v<T, BoxShape>)
                return internal::capsuleSweepVsBox(origin, dir, capsuleRadius, capsuleHalfHeight, b.position, b.rotation, shape.halfExtents);
            else return -1.0f;
        }, b.collider.shape);

        if (t >= 0.0f && t < best.distance) {
            best.hasHit   = true;
            best.distance = t;
            best.entity   = b.entity;
            best.point    = origin + dir * t;
            best.normal   = -dir; // approximate
        }
    }
    return best;
}

int PhysicsWorld::overlapSphere(const Vec3& center, float radius,
                                 const QueryFilter& filter,
                                 Entity* outEntities, int maxResults) const {
    int count = 0;
    for (const BodyData& b : impl_->bodies) {
        if (count >= maxResults) break;
        if (!filter.collides(b.collider.layerIndex, b.collider.layerIndex)) continue;
        if (b.isCharacterController) continue;

        const bool overlaps = std::visit([&](const auto& shape) -> bool {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, SphereShape>)
                return internal::sphereOverlapsSphere(center, radius, b.position, shape.radius);
            else if constexpr (std::is_same_v<T, BoxShape>)
                return internal::sphereOverlapsBox(center, radius, b.position, b.rotation, shape.halfExtents);
            else return false;
        }, b.collider.shape);

        if (overlaps) outEntities[count++] = b.entity;
    }
    return count;
}

} // namespace engine::physics
