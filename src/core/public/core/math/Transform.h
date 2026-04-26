#pragma once

#include "core/math/Mat.h"
#include "core/math/Quat.h"
#include "core/math/Vec.h"

namespace engine::core::math {

    struct Transform {
        Vec3 position{0.0f, 0.0f, 0.0f};
        Quat rotation{Quat::identity()};
        Vec3 scale{1.0f, 1.0f, 1.0f};

        Mat4 toMatrix() const noexcept;

        static Transform fromMatrix(const Mat4& m) noexcept;
    };

    Transform compose(const Transform& parent, const Transform& child) noexcept;

}
