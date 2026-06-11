#ifdef ENGINE_DEVREL

#include "editor/commands/MaterialChangeCommand.h"

#include <core/ecs/World.h>
#include <core/components/MeshHandle.h>

namespace engine::editor {

MaterialChangeCommand::MaterialChangeCommand(core::ecs::World& world,
                                             core::ecs::Entity entity,
                                             uint32_t          oldIndex,
                                             uint32_t          newIndex,
                                             bool              oldCastShadow,
                                             bool              newCastShadow,
                                             bool              oldReceiveShadow,
                                             bool              newReceiveShadow)
    : world_(&world)
    , entity_(entity)
    , oldIndex_(oldIndex)
    , newIndex_(newIndex)
    , oldCastShadow_(oldCastShadow)
    , newCastShadow_(newCastShadow)
    , oldReceiveShadow_(oldReceiveShadow)
    , newReceiveShadow_(newReceiveShadow)
{}

void MaterialChangeCommand::apply(uint32_t matIndex, bool castShadow, bool receiveShadow)
{
    if (!world_->isAlive(entity_)) return;
    if (auto* m = world_->tryGet<core::MeshHandle>(entity_)) {
        m->materialIndex  = matIndex;
        m->castShadow     = castShadow;
        m->receiveShadow  = receiveShadow;
    }
}

void MaterialChangeCommand::execute() { apply(newIndex_, newCastShadow_, newReceiveShadow_); }
void MaterialChangeCommand::undo()    { apply(oldIndex_, oldCastShadow_, oldReceiveShadow_); }

} // namespace engine::editor

#endif // ENGINE_DEVREL
