#pragma once

#include <physics/PhysicsWorld.h>
#include <core/ecs/Entity.h>
#include <core/components/Transform.h>
#include <span>
#include <vector>

namespace engine::physics {

// A single entity's transform snapshot — used to communicate history data to
// LagCompensator without introducing a dependency on engine::networking.
// Callers (e.g. DamageSystem in engine::app) build this from whichever
// history source they own (e.g. ReplicationSystem::historyRing_).
struct EntityTransformSnapshot {
    engine::core::ecs::Entity  entity    = engine::core::ecs::kInvalidEntity;
    engine::core::Transform    transform = {};
};

// Performs lag-compensated raycasts on behalf of server-side hitscan
// validation.  The caller is responsible for:
//   1. Collecting the historic entity positions at the target tick.
//   2. Passing them as an EntityTransformSnapshot span.
//
// LagCompensator temporarily repositions every supplied entity in the
// physics world, executes the raycast, then restores all bodies to their
// original positions before returning.
//
// Thread-safety: not thread-safe; call only from the server tick thread.
class LagCompensator {
public:
    // Construct with the physics world to operate on.
    explicit LagCompensator(PhysicsWorld& physics) noexcept;

    // Rewind `physics` bodies to the positions in `history`, fire a raycast
    // from `origin` along `dir` up to `maxDist`, then restore all bodies.
    // Returns the same RaycastHit as PhysicsWorld::raycast().
    // If `history` is empty the call degrades to a live-position raycast.
    RaycastHit rewindAndRaycast(std::span<const EntityTransformSnapshot> history,
                                const engine::core::math::Vec3&          origin,
                                const engine::core::math::Vec3&          dir,
                                float                                     maxDist,
                                const QueryFilter&                        filter = {});

private:
    PhysicsWorld& physics_;

    // Per-call save buffer (member to avoid repeated heap allocation on the
    // hot path; cleared and reused each call).
    struct Saved {
        engine::core::ecs::Entity entity;
        engine::core::Transform   original;
    };
    std::vector<Saved> saved_;
};

} // namespace engine::physics
