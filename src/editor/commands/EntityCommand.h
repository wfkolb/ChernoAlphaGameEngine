#pragma once
#ifdef ENGINE_DEVREL

#include "editor/UndoStack.h"

#include <core/ecs/Entity.h>
#include <core/components/Transform.h>

#include <string>

namespace engine::core::ecs { class World; }

namespace engine::editor {

// Creates an entity with an optional Name + Transform. Undo destroys it.
// On redo the entity is re-created; the live handle is tracked so a later
// DestroyEntityCommand referencing it stays valid.
class CreateEntityCommand : public ICommand {
public:
    CreateEntityCommand(core::ecs::World& world,
                        std::string name,
                        const core::Transform& transform = {});

    void execute() override;
    void undo() override;
    const char* name() const override { return "Create Entity"; }

    // Valid after the first execute(); used by the editor to auto-select.
    core::ecs::Entity entity() const noexcept { return entity_; }

private:
    core::ecs::World* world_;
    std::string       displayName_;
    core::Transform   transform_;
    core::ecs::Entity entity_ = core::ecs::kInvalidEntity;
};

// Destroys an existing entity, capturing its Name + Transform so undo can
// restore an equivalent entity. The restored entity gets a fresh handle.
class DestroyEntityCommand : public ICommand {
public:
    DestroyEntityCommand(core::ecs::World& world, core::ecs::Entity entity);

    void execute() override;
    void undo() override;
    const char* name() const override { return "Delete Entity"; }

    core::ecs::Entity entity() const noexcept { return entity_; }

private:
    core::ecs::World* world_;
    core::ecs::Entity entity_;
    std::string       displayName_;
    core::Transform   transform_;
    bool              hadTransform_ = false;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
