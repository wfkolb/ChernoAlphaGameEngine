#pragma once
#ifdef ENGINE_DEVREL

#include "core/ecs/World.h"
#include "core/ecs/Entity.h"

namespace engine::tools::internal {

// Draws the entity list panel. Returns the selected entity (kInvalidEntity if none).
engine::core::ecs::Entity drawEntityListPanel(
    engine::core::ecs::World& world,
    engine::core::ecs::Entity currentSelection);

} // namespace engine::tools::internal

#endif // ENGINE_DEVREL
