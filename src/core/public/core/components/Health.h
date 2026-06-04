#pragma once
#include <core/ecs/Entity.h>

namespace engine::core {

struct Health {
    static constexpr ecs::ComponentTypeId kComponentId = 3;

    float currentHp     = 100.0f;
    float maxHp         = 100.0f;
    float shieldPercent = 0.0f;     // [0, 1]: fraction of damage absorbed by shield
};

} // namespace engine::core
