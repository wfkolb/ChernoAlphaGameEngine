#ifdef ENGINE_DEVREL

#include "editor/commands/TransformCommand.h"

#include <core/ecs/World.h>

namespace engine::editor {

TransformCommand::TransformCommand(core::ecs::World& world,
                                   core::ecs::Entity entity,
                                   const core::Transform& before,
                                   const core::Transform& after)
    : world_(&world), entity_(entity), before_(before), after_(after) {}

void TransformCommand::apply(const core::Transform& t) {
    if (!world_->isAlive(entity_)) return;
    if (auto* tr = world_->tryGet<core::Transform>(entity_)) {
        *tr = t;
    }
}

void TransformCommand::execute() { apply(after_); }
void TransformCommand::undo()    { apply(before_); }

} // namespace engine::editor

#endif // ENGINE_DEVREL
