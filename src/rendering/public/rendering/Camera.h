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
    // System reads InputSystem::state(); writes Transform::position/rotation.
    // Movement: WASD + Q/E. Sprint: Shift. Pitch clamped to ±89 degrees.
    // kComponentId = 17: registered after Camera (16).
    struct FpsCameraController {
        static constexpr engine::core::ecs::ComponentTypeId kComponentId = 17;

        float   moveSpeed        { 5.0f };   // metres per second
        float   lookSensitivity  { 0.1f };   // degrees per raw mouse unit
        float   yaw              { 0.0f };   // degrees, world Y-axis
        float   pitch            { 0.0f };   // degrees, clamped ±89
        float   eyeHeight        { 1.7f };   // metres above spawn-point origin
        float   verticalVelocity { 0.0f };   // m/s, integrated by FpsCameraSystem gravity
        bool    active           { true };   // when false, system skips this entity
        bool    isGrounded       { false };  // set by FpsCameraSystem ground check
        uint8_t _pad0            { 0 };
        uint8_t _pad1            { 0 };
    };

    static_assert(sizeof(FpsCameraController) == 28,
        "FpsCameraController layout changed — update padding or serialisation");

    // View matrix from a camera's world transform (right-handed, Y-up).
    core::math::Mat4 cameraViewMatrix(const core::math::Transform& t) noexcept;

    // Reverse-Z perspective projection (nearZ -> depth 1.0, farZ -> depth 0.0).
    core::math::Mat4 cameraProjMatrix(const Camera& cam, float aspectRatio) noexcept;

} // namespace engine::rendering
