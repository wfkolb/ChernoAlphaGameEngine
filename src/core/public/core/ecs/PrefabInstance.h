#pragma once
#include <core/ecs/Entity.h>
#include <cstdint>
#include <type_traits>

namespace engine::core::ecs {

struct PrefabInstance {
    static constexpr ComponentTypeId kComponentId = 13;

    char     sourcePrefabPath[256] = {};
    uint32_t overriddenComponents  = 0;
};

static_assert(std::is_trivially_copyable_v<PrefabInstance>,
              "PrefabInstance must be trivially copyable for ECS archetype moves");

} // namespace engine::core::ecs
