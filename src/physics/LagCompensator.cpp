#include "physics/LagCompensator.h"

namespace engine::physics {

LagCompensator::LagCompensator(PhysicsWorld& physics) noexcept
    : physics_(physics)
{}

RaycastHit LagCompensator::rewindAndRaycast(
    std::span<const EntityTransformSnapshot> history,
    const engine::core::math::Vec3&          origin,
    const engine::core::math::Vec3&          dir,
    float                                    maxDist,
    const QueryFilter&                       filter)
{
    // --- 1. Save current physics positions and apply historic transforms. ----
    saved_.clear();
    saved_.reserve(history.size());

    for (const EntityTransformSnapshot& snap : history) {
        if (snap.entity == engine::core::ecs::kInvalidEntity)
            continue;

        // Read the current (live) transform from the physics world before
        // overwriting it so we can restore it after the raycast.
        const engine::core::Transform live =
            physics_.getTransformByEntity(snap.entity);

        saved_.push_back(Saved{snap.entity, live});

        // Temporarily move the body to its historic position.
        physics_.setTransformByEntity(snap.entity, snap.transform);
    }

    // --- 2. Raycast against the rewound scene. ------------------------------
    RaycastHit result = physics_.raycast(origin, dir, maxDist, filter);

    // --- 3. Restore live positions. ----------------------------------------
    for (const Saved& s : saved_) {
        physics_.setTransformByEntity(s.entity, s.original);
    }

    return result;
}

} // namespace engine::physics
