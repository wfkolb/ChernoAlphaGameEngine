#pragma once

#include <core/ecs/Entity.h>
#include <core/math/Mat.h>
#include <core/math/Transform.h>

namespace engine::rendering {

    // ECS component — attached to a camera entity.
    // The renderer renders from the first entity where isMain == true.
    // Projection: perspective, reverse-Z, right-handed, Y-up.
    // nearZ maps to depth 1.0; farZ maps to depth 0.0.
    // kComponentId = 16: follows the last planned Phase 9 component (15 = TriggerComponent).
    struct Camera {
        static constexpr engine::core::ecs::ComponentTypeId kComponentId = 16;

        float fovYDegrees { 60.0f };
        float nearZ       {  0.1f };
        float farZ        { 1000.0f };
        bool  isMain      { true };
    };

    // ECS component — attach alongside Camera to enable FPS-style mouse look.
    // System reads core::events::RawInputState; writes Transform::position/rotation.
    // Movement: WASD + Q/E. Sprint: Shift. Pitch clamped to ±89 degrees.
    struct FpsCameraController {
        float moveSpeed       { 5.0f };   // metres per second
        float lookSensitivity { 0.1f };   // degrees per raw mouse unit
        float yaw             { 0.0f };   // degrees, world Y-axis
        float pitch           { 0.0f };   // degrees, clamped ±89
    };

    // View matrix from a camera's world transform (right-handed, Y-up).
    core::math::Mat4 cameraViewMatrix(const core::math::Transform& t) noexcept;

    // Reverse-Z perspective projection (nearZ -> depth 1.0, farZ -> depth 0.0).
    core::math::Mat4 cameraProjMatrix(const Camera& cam, float aspectRatio) noexcept;

    // System tick: reads InputSystem state, updates transform (position/rotation)
    // and the FpsCameraController's stored yaw/pitch.
    // Called once per frame in the Update phase for every entity that has both
    // FpsCameraController and Transform components.
    void fpsCameraUpdate(FpsCameraController& ctrl,
                         core::math::Transform& transform,
                         float dt) noexcept;

} // namespace engine::rendering
