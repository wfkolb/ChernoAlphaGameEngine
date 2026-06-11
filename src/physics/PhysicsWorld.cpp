#define NOMINMAX
#include "physics/PhysicsWorld.h"
#include <core/Profiler.h>
#include "BroadPhase.h"
#include "NarrowPhase.h"
#include "CharacterControllerImpl.h"
#include <core/log.h>
#include <core/math/Constants.h>
#include <core/TaskScheduler.h>
#include <core/EventBus.h>
#include <unordered_map>
#include <unordered_set>
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

    // Shared thread pool for parallel broad/narrow-phase work.
    engine::core::TaskScheduler scheduler;

    // ── Trigger volume records ────────────────────────────────────────────────
    struct TriggerBody {
        core::ecs::Entity entity;
        core::math::Vec3  center;
        float             halfX, halfY, halfZ;
        bool              isSphere;
        uint8_t           teamFilter;
        uint32_t          eventTag;
    };
    std::vector<TriggerBody>         triggers;
    // Active overlaps: key = (triggerEntity.index << 32) | dynamicBody.index
    // Value stores the full Entity pair (including generation) for correct event publication.
    struct OverlapRecord { Entity triggerEntity; Entity dynamicEntity; };
    std::unordered_map<uint64_t, OverlapRecord> activeOverlaps;

    core::EventBus* eventBus = nullptr;

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
            if (!b.collider.enabled) continue;
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

engine::core::Transform PhysicsWorld::getTransformByEntity(Entity entity) const {
    for (const BodyData& b : impl_->bodies) {
        if (b.entity != entity) continue;
        engine::core::Transform t;
        t.position = b.position;
        t.rotation = b.rotation;
        return t;
    }
    return {};
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
    PROFILE_SCOPE("PhysicsWorld::step");
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

    // 2. Collision detection: broad-phase query + narrow-phase per dynamic body.
    //    Each dynamic body is independent (reads-only from static BVH/bodies),
    //    so we partition the dynamic-body index list across worker tasks and
    //    accumulate contacts per-task, then merge before the constraint solver.

    struct ContactInfo {
        size_t  dynIdx;
        Vec3    normal;
        float   depth;
    };

    // Helper: run the narrow-phase dispatch for one dyn/stat pair.
    auto narrowPhase = [&](const BodyData& dyn, const BodyData& stat)
        -> internal::ContactManifold
    {
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
                    return internal::ContactManifold{};
                }
            }, stat.collider.shape);
        }, dyn.collider.shape);
    };

    // Collect the indices of all dynamic bodies once.
    // Bodies whose collider is disabled are excluded from broad/narrow phase.
    std::vector<size_t> dynIndices;
    dynIndices.reserve(impl_->bodies.size());
    for (size_t i = 0; i < impl_->bodies.size(); ++i) {
        const BodyData& b = impl_->bodies[i];
        if (!b.isCharacterController && b.rb.type == RigidBodyType::Dynamic
            && b.collider.enabled)
            dynIndices.push_back(i);
    }

    // Partition the dynamic-body index list into per-worker chunks.
    // Each task writes to its own contacts sub-vector (no shared writes).
    const unsigned int workerCount = impl_->scheduler.threadCount();
    const size_t       dynCount    = dynIndices.size();

    // Per-task contact storage; sized to workerCount before any submit().
    std::vector<std::vector<ContactInfo>> perTaskContacts(workerCount);

    for (unsigned int t = 0; t < workerCount; ++t) {
        // Compute the inclusive slice [begin, end) for this task.
        const size_t begin = (dynCount *  t     ) / workerCount;
        const size_t end   = (dynCount * (t + 1)) / workerCount;
        if (begin >= end) continue; // empty slice — no task needed

        // Capture by value what each task needs (read-only pointers to impl_).
        const std::vector<BodyData>*  bodies      = &impl_->bodies;
        const internal::StaticBVH*    staticBVH   = &impl_->staticBVH;
        std::vector<ContactInfo>*     taskContacts = &perTaskContacts[t];

        // The future is intentionally discarded: we synchronise via wait() below.
        (void)impl_->scheduler.submit(
            [bodies, staticBVH, taskContacts, &dynIndices, &narrowPhase,
             begin, end]()
        {
            for (size_t si = begin; si < end; ++si) {
                const size_t        di  = dynIndices[si];
                const BodyData&     dyn = (*bodies)[di];

                const AABB dynAABB = internal::computeAABB(
                    dyn.collider, dyn.position, dyn.rotation);

                std::vector<uint32_t> candidates;
                staticBVH->query(dynAABB, candidates);

                for (uint32_t ci : candidates) {
                    const BodyData& stat = (*bodies)[ci];
                    const internal::ContactManifold manifold =
                        narrowPhase(dyn, stat);
                    for (int c = 0; c < manifold.count; ++c) {
                        if (manifold.contacts[c].depth > 0.0f) {
                            taskContacts->push_back({
                                di,
                                manifold.contacts[c].normal,
                                manifold.contacts[c].depth
                            });
                        }
                    }
                }
            }
        });
    }

    // Wait for all tasks, then merge contacts.
    impl_->scheduler.wait();

    std::vector<ContactInfo> contacts;
    for (auto& tc : perTaskContacts) {
        contacts.insert(contacts.end(), tc.begin(), tc.end());
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

    // ── Trigger overlap detection ─────────────────────────────────────────────
    if (impl_->eventBus && !impl_->triggers.empty()) {
        std::unordered_map<uint64_t, Impl::OverlapRecord> currentOverlaps;

        for (const auto& trigger : impl_->triggers) {
            for (const auto& body : impl_->bodies) {
                // Skip non-dynamic bodies and the trigger entity itself.
                if (body.rb.type != RigidBodyType::Dynamic) continue;
                if (body.entity == trigger.entity) continue;

                // Overlap test: sphere triggers use halfX as radius;
                // box triggers use a conservative AABB expansion of 0.3m
                // (approximates body half-extent without full shape query).
                const bool overlaps = trigger.isSphere
                    ? (length(body.position - trigger.center) <= trigger.halfX + 0.5f)
                    : (std::abs(body.position.x - trigger.center.x) <= trigger.halfX + 0.3f &&
                       std::abs(body.position.y - trigger.center.y) <= trigger.halfY + 0.3f &&
                       std::abs(body.position.z - trigger.center.z) <= trigger.halfZ + 0.3f);

                if (!overlaps) continue;

                const uint64_t key =
                    (static_cast<uint64_t>(trigger.entity.index) << 32) |
                    static_cast<uint64_t>(body.entity.index);
                currentOverlaps[key] = { trigger.entity, body.entity };

                // Fire enter event if this is a new overlap.
                if (impl_->activeOverlaps.find(key) == impl_->activeOverlaps.end()) {
                    impl_->eventBus->publish(TriggerEnterEvent{
                        trigger.entity, body.entity, trigger.eventTag });
                }
            }
        }

        // Fire exit events for overlaps that ended this step.
        for (const auto& [key, rec] : impl_->activeOverlaps) {
            if (currentOverlaps.find(key) == currentOverlaps.end()) {
                uint32_t tag = 0;
                for (const auto& t : impl_->triggers)
                    if (t.entity == rec.triggerEntity) { tag = t.eventTag; break; }
                impl_->eventBus->publish(TriggerExitEvent{
                    rec.triggerEntity, rec.dynamicEntity, tag });
            }
        }

        impl_->activeOverlaps = std::move(currentOverlaps);
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

// ── Trigger volume management ─────────────────────────────────────────────────

void PhysicsWorld::setEventBus(core::EventBus* bus) noexcept {
    impl_->eventBus = bus;
}

void PhysicsWorld::addTrigger(const TriggerDesc& desc) {
    // Remove any existing entry for this entity first (idempotent).
    removeTrigger(desc.entity);
    impl_->triggers.push_back({
        desc.entity,
        desc.center,
        desc.halfX, desc.halfY, desc.halfZ,
        desc.isSphere,
        static_cast<uint8_t>(desc.teamFilter),
        desc.eventTag
    });
}

void PhysicsWorld::removeTrigger(Entity entity) {
    auto& ts = impl_->triggers;
    ts.erase(
        std::remove_if(ts.begin(), ts.end(),
            [entity](const Impl::TriggerBody& t) { return t.entity == entity; }),
        ts.end());
    // Clear any active overlaps involving this trigger.
    for (auto it = impl_->activeOverlaps.begin(); it != impl_->activeOverlaps.end(); ) {
        if (it->second.triggerEntity.index == entity.index)
            it = impl_->activeOverlaps.erase(it);
        else
            ++it;
    }
}

void PhysicsWorld::updateTrigger(Entity entity, const Vec3& newCenter) {
    for (auto& t : impl_->triggers) {
        if (t.entity == entity) {
            t.center = newCenter;
            return;
        }
    }
}

} // namespace engine::physics
