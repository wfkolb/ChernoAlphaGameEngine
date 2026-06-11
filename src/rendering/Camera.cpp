#include <rendering/Camera.h>
#include <core/math/Mat.h>
#include <core/math/Constants.h>
#include <core/math/Transform.h>


namespace engine::rendering {

using namespace engine::core::math;

Mat4 cameraViewMatrix(const Transform& t) noexcept
{
    // Directly invert the camera's world transform: transpose the rotation
    // part and negate the projected translation.  Equivalent to lookAtRh but
    // uses the full quaternion orientation rather than reconstructing it from
    // forward + world-up, which avoids the near-singularity at ±90° pitch.
    return affineInverse(t.toMatrix());
}

Mat4 cameraProjMatrix(const Camera& cam, float aspectRatio) noexcept
{
    const float fovYRad = cam.fovYDegrees * kDegToRad;
    return perspectiveRhYupReverseZ(fovYRad, aspectRatio, cam.nearZ, cam.farZ);
}

} // namespace engine::rendering
