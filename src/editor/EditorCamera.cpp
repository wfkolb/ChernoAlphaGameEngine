#ifdef ENGINE_DEVREL

#include "editor/EditorCamera.h"

#include <core/math/Quat.h>

#include <algorithm>
#include <cmath>

namespace engine::editor {

using core::math::Vec3;
using core::math::Quat;

namespace {
constexpr float kPitchLimit = 1.55334f; // ~89 degrees in radians
}

void EditorCamera::recomputePosition() {
    // Camera sits on a sphere of radius distance_ around focus_, oriented by
    // yaw/pitch. forward points from camera toward focus.
    const Quat rot     = core::math::fromEulerYxz(yaw_, pitch_, 0.0f);
    const Vec3 forward = core::math::rotate(rot, Vec3{0.0f, 0.0f, -1.0f});
    position_ = focus_ - forward * distance_;
}

void EditorCamera::update(const Input& in, float dt) {
    if (in.frameRequested) {
        frame(focus_, distance_ * 0.5f);
    }

    if (in.altDown && in.rightMouseDown) {
        // Orbit around focus.
        yaw_   -= in.mouseDeltaX * orbitSensitivity;
        pitch_ -= in.mouseDeltaY * orbitSensitivity;
        pitch_  = std::clamp(pitch_, -kPitchLimit, kPitchLimit);
    } else if (in.rightMouseDown) {
        // Fly: mouse looks, WASDQE translates the focus along camera axes.
        yaw_   -= in.mouseDeltaX * lookSensitivity;
        pitch_ -= in.mouseDeltaY * lookSensitivity;
        pitch_  = std::clamp(pitch_, -kPitchLimit, kPitchLimit);

        const Quat rot     = core::math::fromEulerYxz(yaw_, pitch_, 0.0f);
        const Vec3 forward = core::math::rotate(rot, Vec3{0.0f, 0.0f, -1.0f});
        const Vec3 right   = core::math::rotate(rot, Vec3{1.0f, 0.0f, 0.0f});
        const Vec3 worldUp = Vec3{0.0f, 1.0f, 0.0f};

        float speed = moveSpeed * (in.sprint ? sprintMultiplier : 1.0f);
        Vec3  move  = forward * in.moveForward + right * in.moveRight + worldUp * in.moveUp;
        focus_ += move * (speed * dt);
    }

    // Wheel dollies in/out by scaling the orbit distance.
    if (in.scrollDelta != 0.0f) {
        const float factor = std::pow(0.9f, in.scrollDelta);
        distance_ = std::clamp(distance_ * factor, 0.1f, 5000.0f);
    }

    recomputePosition();
}

void EditorCamera::frame(const Vec3& target, float radius) {
    focus_    = target;
    distance_ = std::max(radius * 2.0f, 0.5f);
    recomputePosition();
}

void EditorCamera::setFirstPersonView(const core::math::Vec3& position,
                                       float yawRad, float pitchRad) noexcept {
    position_ = position;
    yaw_      = yawRad;
    pitch_    = std::clamp(pitchRad, -kPitchLimit, kPitchLimit);
    // Orbit distance of ~zero keeps focus just in front of the camera so
    // recomputePosition() does not displace position_ significantly.
    distance_ = 0.001f;
    const Quat rot     = core::math::fromEulerYxz(yaw_, pitch_, 0.0f);
    const Vec3 forward = core::math::rotate(rot, Vec3{0.0f, 0.0f, -1.0f});
    focus_ = position_ + forward;
}

core::math::Mat4 EditorCamera::viewMatrix() const {
    return core::math::lookAtRh(position_, focus_, Vec3{0.0f, 1.0f, 0.0f});
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
