#pragma once
#include <core/ecs/Entity.h>
#include <cstdint>

namespace engine::networking {

struct NetworkIdentity {
    static constexpr engine::core::ecs::ComponentTypeId kComponentId = 8u;
    static constexpr uint32_t kServerOwned = 0xFFFFFFFFu;

    uint32_t netId                = 0u;
    uint32_t ownerClientId        = kServerOwned;
    uint32_t replicatedComponents = 0u;
    float    relevanceRadius      = 100.0f;
};

} // namespace engine::networking
