#pragma once
#ifdef ENGINE_DEVREL

#include <core/math/Vec.h>
#include <core/math/Mat.h>

namespace engine::editor {

// Free-fly / orbit camera used by the viewport. Holds a focus point + spherical
// offset so orbit (Alt+drag) and fly (right-mouse WASD) share one state.
//
// Per-frame input is supplied via EditorCamera::Input; the panel translates
// ImGui mouse/keyboard state into it. Angles are in radians.
class EditorCamera {
public:
    struct Input {
        bool  rightMouseDown = false;   // fly mode (WASD + look)
        bool  altDown        = false;   // orbit modifier
        bool  frameRequested = false;   // F: frame the focus/selection
        float mouseDeltaX    = 0.0f;    // pixels
        float mouseDeltaY    = 0.0f;
        float scrollDelta    = 0.0f;    // wheel notches (dolly)
        float moveForward    = 0.0f;    // -1..1 (W/S)
        float moveRight      = 0.0f;    // -1..1 (D/A)
        float moveUp         = 0.0f;    // -1..1 (E/Q)
        bool  sprint         = false;   // Shift
    };

    void update(const Input& in, float dt);

    // Re-center on a point and (optionally) pull the camera to a framing distance.
    void frame(const core::math::Vec3& target, float radius = 2.0f);

    // Overwrite position and orientation directly. Used by PIE to mirror the
    // player entity Transform into the viewport without disturbing orbit state.
    void setFirstPersonView(const core::math::Vec3& position, float yawRad, float pitchRad) noexcept;

    core::math::Mat4 viewMatrix() const;
    core::math::Vec3 position() const { return position_; }
    core::math::Vec3 focus() const { return focus_; }

    float fovYDegrees = 60.0f;
    float nearZ       = 0.05f;
    float farZ        = 2000.0f;

    float moveSpeed       = 6.0f;    // metres/sec
    float sprintMultiplier = 4.0f;
    float lookSensitivity = 0.0045f; // radians per pixel
    float orbitSensitivity = 0.008f;

private:
    void recomputePosition();

    core::math::Vec3 focus_    { 0.0f, 0.0f, 0.0f };
    core::math::Vec3 position_ { 0.0f, 2.0f, 6.0f };
    float yaw_      = 0.0f;     // radians around world Y
    float pitch_    = -0.15f;   // radians; clamped to avoid gimbal flip
    float distance_ = 6.0f;     // focus-to-camera distance (orbit radius)
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
