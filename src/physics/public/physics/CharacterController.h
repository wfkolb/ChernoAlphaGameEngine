#pragma once

#include <core/ecs/Entity.h>
#include <core/math/Vec.h>

namespace engine::physics {

struct CharacterController {
    static constexpr engine::core::ecs::ComponentTypeId kComponentId = 7;

    float capsuleRadius   = 0.3f;
    float capsuleHeight   = 1.8f;   // total height including both hemispheres
    float stepUpHeight    = 0.35f;
    float maxSlopeAngle   = 45.0f;  // degrees

    bool  isGrounded = false;
    engine::core::math::Vec3 desiredVelocity = engine::core::math::Vec3::zero();

    float coyoteTime  = 0.0f;  // seconds remaining in coyote-time window
    float jumpBuffer  = 0.0f;  // seconds remaining in jump-buffer window
};

} // namespace engine::physics
