#pragma once

#include <physics/ColliderShape.h>
#include <physics/RigidBody.h>
#include <physics/CharacterController.h>
#include <physics/QueryFilter.h>
#include <core/components/Transform.h>
#include <core/ecs/Entity.h>
#include <core/math/Vec.h>
#include <memory>

namespace engine::physics {

using BodyId = uint32_t;
constexpr BodyId kInvalidBodyId = ~0u;

struct RaycastHit {
    engine::core::ecs::Entity entity = engine::core::ecs::kInvalidEntity;
    engine::core::math::Vec3  point  = engine::core::math::Vec3::zero();
    engine::core::math::Vec3  normal = engine::core::math::Vec3::unitY();
    float distance = 0.0f;
    bool  hasHit   = false;
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&)            = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&)                 = delete;
    PhysicsWorld& operator=(PhysicsWorld&&)      = delete;

    // Gravity (default: {0, -9.81, 0})
    void setGravity(engine::core::math::Vec3 g) noexcept;
    engine::core::math::Vec3 getGravity() const noexcept;

    // Rigid body management
    BodyId addRigidBody(engine::core::ecs::Entity entity,
                        const engine::core::Transform& transform,
                        const RigidBody& rb,
                        const Collider& collider);
    void removeBody(BodyId id);

    // Read/write transform of an existing body (for kinematic or after-step sync)
    engine::core::Transform getTransform(BodyId id) const;
    void setTransform(BodyId id, const engine::core::Transform& t);

    // Move every physics body that belongs to `entity` to `t`. Used by the
    // lag-compensation rewind to temporarily reposition bodies before a raycast
    // and by kinematic sync after the ECS hierarchy propagation pass.
    void setTransformByEntity(engine::core::ecs::Entity entity,
                              const engine::core::Transform& t);

    // Return the transform of the first body belonging to `entity`, or a
    // default-constructed Transform if no matching body is found.
    // Used by LagCompensator to save/restore positions around a rewind.
    engine::core::Transform getTransformByEntity(engine::core::ecs::Entity entity) const;

    // Velocity / force (ignored for Static bodies)
    void setLinearVelocity(BodyId id, engine::core::math::Vec3 v);
    engine::core::math::Vec3 getLinearVelocity(BodyId id) const;
    void applyForce(BodyId id, engine::core::math::Vec3 force);
    void applyImpulse(BodyId id, engine::core::math::Vec3 impulse);

    // Character controller management
    BodyId addCharacterController(engine::core::ecs::Entity entity,
                                  const engine::core::Transform& transform,
                                  const CharacterController& cc);
    void setDesiredVelocity(BodyId id, engine::core::math::Vec3 v);
    bool isGrounded(BodyId id) const;

    // Advance simulation one fixed step.
    // Callers are responsible for accumulating time and calling at 64 Hz.
    void step(float dt);

    // Queries (safe to call any time, including between steps)
    RaycastHit raycast(const engine::core::math::Vec3& origin,
                       const engine::core::math::Vec3& dir,
                       float maxDist,
                       const QueryFilter& filter = {}) const;

    RaycastHit sweepCapsule(const engine::core::math::Vec3& origin,
                            const engine::core::math::Vec3& dir,
                            float capsuleRadius,
                            float capsuleHalfHeight,
                            float maxDist,
                            const QueryFilter& filter = {}) const;

    // Returns number of overlapping entities written to outEntities (capped at maxResults).
    int overlapSphere(const engine::core::math::Vec3& center,
                      float radius,
                      const QueryFilter& filter,
                      engine::core::ecs::Entity* outEntities,
                      int maxResults) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::physics
