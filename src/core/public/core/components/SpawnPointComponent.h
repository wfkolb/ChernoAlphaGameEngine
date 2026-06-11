#pragma once
#include <core/ecs/Entity.h>
#include <type_traits>
#include <cstdint>

namespace engine::core {

struct SpawnPointComponent {
    static constexpr ecs::ComponentTypeId kComponentId = 14;

    uint8_t teamId   = 0;      // 0 = any team
    uint8_t priority = 0;      // higher = preferred
    uint8_t _pad0    = 0;
    uint8_t _pad1    = 0;
    float   radius   = 1.0f;   // exclusion zone radius in metres
};

static_assert(std::is_trivially_copyable_v<SpawnPointComponent>);

} // namespace engine::core
