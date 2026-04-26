#include <rendering/Camera.h>
#include <core/math/Mat.h>
#include <core/math/Quat.h>
#include <core/math/Constants.h>
#include <core/math/Transform.h>
#include <core/Input.h>


namespace engine::rendering {

using namespace engine::core::math;

// ---------------------------------------------------------------------------
// Camera utility free functions
// ---------------------------------------------------------------------------

Mat4 cameraViewMatrix(const Transform& t) noexcept
{
    // Right-handed lookAt: eye = t.position, look along -Z of the transform.
    const Vec3 forward = rotate(t.rotation, {0.0f, 0.0f, -1.0f});
    return lookAtRh(t.position, t.position + forward, {0.0f, 1.0f, 0.0f});
}

Mat4 cameraProjMatrix(const Camera& cam, float aspectRatio) noexcept
{
    const float fovYRad = cam.fovYDegrees * kDegToRad;
    return perspectiveRhYupReverseZ(fovYRad, aspectRatio, cam.nearZ, cam.farZ);
}

// ---------------------------------------------------------------------------
// FpsCameraController update
// ---------------------------------------------------------------------------

void fpsCameraUpdate(FpsCameraController& ctrl,
                     Transform&           transform,
                     float                dt) noexcept
{
    using core::InputSystem;
    using core::Key;

    const core::InputState& input = InputSystem::state();

    // --- Mouse look ---
    const float dx = static_cast<float>(input.mouseDeltaX()) * ctrl.lookSensitivity;
    const float dy = static_cast<float>(input.mouseDeltaY()) * ctrl.lookSensitivity;

    ctrl.yaw   += dx;
    ctrl.pitch  = clamp(ctrl.pitch + dy, -89.0f, 89.0f);

    // Build orientation from yaw (world Y) and pitch (local X), YXZ order.
    const Quat orientation = fromEulerYxz(ctrl.yaw   * kDegToRad,
                                          ctrl.pitch  * kDegToRad,
                                          0.0f);
    transform.rotation = normalize(orientation);

    // --- Movement ---
    const Vec3 forward = rotate(transform.rotation, {0.0f, 0.0f, -1.0f});
    const Vec3 right   = rotate(transform.rotation, {1.0f, 0.0f,  0.0f});
    constexpr Vec3 worldUp{0.0f, 1.0f, 0.0f};

    Vec3 move{};
    if (input.isKeyDown(Key::W)) move = move + forward;
    if (input.isKeyDown(Key::S)) move = move - forward;
    if (input.isKeyDown(Key::D)) move = move + right;
    if (input.isKeyDown(Key::A)) move = move - right;
    if (input.isKeyDown(Key::E)) move = move + worldUp;
    if (input.isKeyDown(Key::Q)) move = move - worldUp;

    const float speed = ctrl.moveSpeed *
                        (input.isKeyDown(Key::LShift) ? 5.0f : 1.0f);
    transform.position = transform.position + move * (speed * dt);
}

} // namespace engine::rendering
