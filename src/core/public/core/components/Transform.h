#pragma once
#include <core/ecs/Entity.h>
#include <core/math/Vec.h>
#include <core/math/Quat.h>

namespace engine::core {

struct Transform {
    static constexpr ecs::ComponentTypeId kComponentId = 1;

    math::Vec3 position = math::Vec3::zero();
    math::Quat rotation = math::Quat::identity();
    math::Vec3 scale    = math::Vec3::one();
};

} // namespace engine::core
