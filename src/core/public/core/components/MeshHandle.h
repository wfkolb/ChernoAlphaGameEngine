#pragma once
#include <core/ecs/Entity.h>
#include <type_traits>

namespace engine::core {

struct MeshHandle {
    static constexpr ecs::ComponentTypeId kComponentId = 12;

    char assetPath[256] = {};
};

static_assert(std::is_trivially_copyable_v<MeshHandle>,
              "MeshHandle must be trivially copyable for ECS archetype moves");

} // namespace engine::core
