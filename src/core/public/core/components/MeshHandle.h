#pragma once
#include <core/ecs/Entity.h>
#include <cstdint>
#include <type_traits>

namespace engine::core {

struct MeshHandle {
    static constexpr ecs::ComponentTypeId kComponentId = 12;

    char assetPath[256] = {};

    // Material index into the material registry (0 = default/fallback material).
    uint32_t materialIndex { 0 };
    bool     castShadow    { true };
    bool     receiveShadow { true };
    uint8_t  _pad0         { 0 };
    uint8_t  _pad1         { 0 };
};

static_assert(std::is_trivially_copyable_v<MeshHandle>,
              "MeshHandle must be trivially copyable for ECS archetype moves");

} // namespace engine::core
