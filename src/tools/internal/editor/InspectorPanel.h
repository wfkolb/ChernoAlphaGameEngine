#pragma once
#ifdef ENGINE_DEVREL

#include "core/ecs/World.h"
#include "core/ecs/Entity.h"

namespace engine::tools::internal {

// Draws the inspector panel for the selected entity.
void drawInspectorPanel(
    engine::core::ecs::World& world,
    engine::core::ecs::Entity entity);

} // namespace engine::tools::internal

#endif // ENGINE_DEVREL
