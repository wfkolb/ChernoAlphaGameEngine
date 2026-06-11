#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "FpsCameraSystem.h"
#include <rendering/Camera.h>
#include <core/ecs/View.h>
#include <core/components/Transform.h>
#include <core/math/Quat.h>
#include <core/math/Vec.h>
#include <core/Input.h>

namespace engine::rendering {

using namespace engine::core::math;

static constexpr float kGravity       = -9.81f;
static constexpr float kGroundEpsilon = 0.001f;

void FpsCameraSystem::tick(core::ecs::World& world, float dt) {
    using core::InputSystem;
    using core::Key;

    const core::InputState& input = InputSystem::state();

    core::ecs::View<core::Transform, FpsCameraController> view(world);
    for (auto [e, tr, ctrl] : view) {
        if (!ctrl.active) continue;

        // --- Mouse look ---
        const float dx = static_cast<float>(input.mouseDeltaX()) * ctrl.lookSensitivity;
        const float dy = static_cast<float>(input.mouseDeltaY()) * ctrl.lookSensitivity;

        ctrl.yaw   -= dx;
        ctrl.pitch  = clamp(ctrl.pitch - dy, -89.0f, 89.0f);

        const Quat orientation = fromEulerYxz(ctrl.yaw   * kDegToRad,
                                              ctrl.pitch  * kDegToRad,
                                              0.0f);
        tr.rotation = normalize(orientation);

        // --- Horizontal movement (XZ only) ---
        // Use yaw-only rotation so WASD stays flat regardless of where the player is looking.
        const Quat yawOnly = fromEulerYxz(ctrl.yaw * kDegToRad, 0.0f, 0.0f);
        const Vec3 forward = rotate(yawOnly, Vec3{0.0f, 0.0f, -1.0f});
        const Vec3 right   = rotate(yawOnly, Vec3{1.0f, 0.0f,  0.0f});

        Vec3 moveXZ{};
        if (input.isKeyDown(Key::W)) moveXZ = moveXZ + forward;
        if (input.isKeyDown(Key::S)) moveXZ = moveXZ - forward;
        if (input.isKeyDown(Key::D)) moveXZ = moveXZ + right;
        if (input.isKeyDown(Key::A)) moveXZ = moveXZ - right;

        const float speed = ctrl.moveSpeed *
                            (input.isKeyDown(Key::LShift) ? 5.0f : 1.0f);
        tr.position = tr.position + moveXZ * (speed * dt);

        // --- Gravity ---
        ctrl.verticalVelocity += kGravity * dt;
        tr.position.y += ctrl.verticalVelocity * dt;

        // Ground plane at Y = 0 (character feet). Camera sits at eyeHeight above feet.
        const float feetY = tr.position.y - ctrl.eyeHeight;
        if (feetY <= kGroundEpsilon) {
            tr.position.y         = ctrl.eyeHeight;
            ctrl.verticalVelocity = 0.0f;
            ctrl.isGrounded       = true;
        } else {
            ctrl.isGrounded = false;
        }
    }
}

} // namespace engine::rendering
