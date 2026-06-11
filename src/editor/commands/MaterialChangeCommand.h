#pragma once
#ifdef ENGINE_DEVREL

#include "editor/UndoStack.h"
#include <core/ecs/Entity.h>
#include <cstdint>

namespace engine::core::ecs { class World; }

namespace engine::editor {

// Records a before/after change to a MeshHandle's material fields.
// Pushed when the user edits materialIndex, castShadow, or receiveShadow
// via the Inspector so the whole edit is a single undo step.
class MaterialChangeCommand : public ICommand {
public:
    MaterialChangeCommand(core::ecs::World& world,
                          core::ecs::Entity entity,
                          uint32_t          oldIndex,
                          uint32_t          newIndex,
                          bool              oldCastShadow,
                          bool              newCastShadow,
                          bool              oldReceiveShadow,
                          bool              newReceiveShadow);

    void execute() override;
    void undo()    override;
    const char* name() const override { return "Material Change"; }

private:
    void apply(uint32_t matIndex, bool castShadow, bool receiveShadow);

    core::ecs::World* world_;
    core::ecs::Entity entity_;
    uint32_t          oldIndex_;
    uint32_t          newIndex_;
    bool              oldCastShadow_;
    bool              newCastShadow_;
    bool              oldReceiveShadow_;
    bool              newReceiveShadow_;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
