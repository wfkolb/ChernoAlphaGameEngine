#pragma once
#include <core/ecs/Entity.h>

namespace engine::core {

struct Lifetime {
    static constexpr ecs::ComponentTypeId kComponentId = 4;

    float remaining = 0.0f;     // seconds; entity is destroyed by the system when <= 0
};

} // namespace engine::core
