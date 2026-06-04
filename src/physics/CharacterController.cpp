#include "CharacterControllerImpl.h"

namespace engine::physics::internal {

using namespace engine::core::math;

float capsuleCylinderHalfHeight(const CharacterController& cc) noexcept {
    const float half = cc.capsuleHeight * 0.5f - cc.capsuleRadius;
    return half > 0.0f ? half : 0.0f;
}

void tickCharacterTimers(CharacterController& cc, float dt) noexcept {
    cc.coyoteTime = cc.coyoteTime > dt ? cc.coyoteTime - dt : 0.0f;
    cc.jumpBuffer = cc.jumpBuffer > dt ? cc.jumpBuffer - dt : 0.0f;
}

void updateGroundedState(CharacterController& cc, bool groundedThisFrame, float dt) noexcept {
    if (!groundedThisFrame && cc.isGrounded) {
        constexpr float kCoyoteWindow = 0.12f;
        cc.coyoteTime = kCoyoteWindow;
    }
    cc.isGrounded = groundedThisFrame;
    tickCharacterTimers(cc, dt);
}

Vec3 slideAlongSurface(const Vec3& v, const Vec3& surfaceNormal) noexcept {
    return v - surfaceNormal * dot(v, surfaceNormal);
}

Vec3 clampGroundedVelocity(const Vec3& desiredVelocity, bool isGrounded) noexcept {
    if (!isGrounded) return desiredVelocity;
    return {desiredVelocity.x,
            desiredVelocity.y < 0.0f ? 0.0f : desiredVelocity.y,
            desiredVelocity.z};
}

} // namespace engine::physics::internal
