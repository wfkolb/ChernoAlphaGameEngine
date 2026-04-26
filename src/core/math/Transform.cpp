#include "core/math/Transform.h"

#include "core/math/Mat.h"
#include "core/math/Quat.h"
#include "core/math/Vec.h"

namespace engine::core::math {

    Mat4 Transform::toMatrix() const noexcept {
        // Build S*R*T directly to avoid three full matrix multiplies.
        // For row vectors: v' = v * S * R * T (scale, then rotate, then translate).
        // The rotation matrix rows are scaled by the scale components,
        // and translation is placed in row 3.
        const Mat4 rot = toMat4(rotation);

        Mat4 r{};
        r.m[0][0] = rot.m[0][0] * scale.x;
        r.m[0][1] = rot.m[0][1] * scale.x;
        r.m[0][2] = rot.m[0][2] * scale.x;
        r.m[0][3] = 0.0f;

        r.m[1][0] = rot.m[1][0] * scale.y;
        r.m[1][1] = rot.m[1][1] * scale.y;
        r.m[1][2] = rot.m[1][2] * scale.y;
        r.m[1][3] = 0.0f;

        r.m[2][0] = rot.m[2][0] * scale.z;
        r.m[2][1] = rot.m[2][1] * scale.z;
        r.m[2][2] = rot.m[2][2] * scale.z;
        r.m[2][3] = 0.0f;

        r.m[3][0] = position.x;
        r.m[3][1] = position.y;
        r.m[3][2] = position.z;
        r.m[3][3] = 1.0f;
        return r;
    }

    Transform Transform::fromMatrix(const Mat4& m) noexcept {
        Transform t{};

        t.position = {m.m[3][0], m.m[3][1], m.m[3][2]};

        // Scale = length of each basis row (upper-left 3x3)
        const Vec3 row0 = {m.m[0][0], m.m[0][1], m.m[0][2]};
        const Vec3 row1 = {m.m[1][0], m.m[1][1], m.m[1][2]};
        const Vec3 row2 = {m.m[2][0], m.m[2][1], m.m[2][2]};

        t.scale.x = length(row0);
        t.scale.y = length(row1);
        t.scale.z = length(row2);

        // Build a pure rotation matrix from the normalised rows, then extract quaternion
        Mat4 rotMat = Mat4::identity();
        if (t.scale.x > kEpsilon) { const Vec3 n = row0 / t.scale.x; rotMat.m[0][0] = n.x; rotMat.m[0][1] = n.y; rotMat.m[0][2] = n.z; }
        if (t.scale.y > kEpsilon) { const Vec3 n = row1 / t.scale.y; rotMat.m[1][0] = n.x; rotMat.m[1][1] = n.y; rotMat.m[1][2] = n.z; }
        if (t.scale.z > kEpsilon) { const Vec3 n = row2 / t.scale.z; rotMat.m[2][0] = n.x; rotMat.m[2][1] = n.y; rotMat.m[2][2] = n.z; }

        t.rotation = fromMat4(rotMat);
        return t;
    }

    Transform compose(const Transform& parent, const Transform& child) noexcept {
        Transform result{};
        result.position  = rotate(parent.rotation, child.position * parent.scale) + parent.position;
        result.rotation  = parent.rotation * child.rotation;
        result.scale     = parent.scale * child.scale;
        return result;
    }

} // namespace engine::core::math
