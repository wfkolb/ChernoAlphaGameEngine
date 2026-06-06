#ifdef ENGINE_DEVREL

#include "editor/commands/ColliderResizeCommand.h"

#include <core/ecs/World.h>

namespace engine::editor {

ColliderResizeCommand::ColliderResizeCommand(core::ecs::World& world,
                                             core::ecs::Entity entity,
                                             const core::ColliderComponent& before,
                                             const core::ColliderComponent& after)
    : world_(&world), entity_(entity), before_(before), after_(after) {}

void ColliderResizeCommand::apply(const core::ColliderComponent& c) {
    if (!world_->isAlive(entity_)) return;
    if (auto* col = world_->tryGet<core::ColliderComponent>(entity_)) {
        *col = c;
    }
}

void ColliderResizeCommand::execute() { apply(after_); }
void ColliderResizeCommand::undo()    { apply(before_); }

} // namespace engine::editor

#endif // ENGINE_DEVREL
