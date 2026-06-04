#pragma once

#include <physics/CharacterController.h>
#include <core/math/Vec.h>

namespace engine::physics::internal {

// Half-height of the cylindrical section (total height minus two hemispheres).
float capsuleCylinderHalfHeight(const CharacterController& cc) noexcept;

// Tick down coyote and jump-buffer timers.
void tickCharacterTimers(CharacterController& cc, float dt) noexcept;

// Update grounded flag and start coyote window when character leaves ground.
void updateGroundedState(CharacterController& cc, bool groundedThisFrame, float dt) noexcept;

// Remove the component of v in the direction of surfaceNormal (slide along surface).
engine::core::math::Vec3 slideAlongSurface(const engine::core::math::Vec3& v,
                                            const engine::core::math::Vec3& surfaceNormal) noexcept;

// Zero the downward component of desiredVelocity when grounded to prevent sinking.
engine::core::math::Vec3 clampGroundedVelocity(const engine::core::math::Vec3& desiredVelocity,
                                                bool isGrounded) noexcept;

} // namespace engine::physics::internal
