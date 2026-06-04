#pragma once
#include <core/ecs/Entity.h>
#include <cstdint>

namespace engine::core {

struct TeamTag {
    static constexpr ecs::ComponentTypeId kComponentId = 5;

    uint8_t teamId = 0;
};

} // namespace engine::core
