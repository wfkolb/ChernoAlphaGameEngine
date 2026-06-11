#pragma once
#include <core/ecs/Entity.h>
#include <cstdint>

namespace engine::physics {

struct TriggerEnterEvent {
    engine::core::ecs::Entity triggerEntity;
    engine::core::ecs::Entity enteringEntity;
    uint32_t                  eventTag = 0;
};

struct TriggerExitEvent {
    engine::core::ecs::Entity triggerEntity;
    engine::core::ecs::Entity leavingEntity;
    uint32_t                  eventTag = 0;
};

} // namespace engine::physics
