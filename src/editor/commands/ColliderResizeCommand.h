#pragma once
#ifdef ENGINE_DEVREL

#include "editor/UndoStack.h"

#include <core/ecs/Entity.h>
#include <core/components/ColliderComponent.h>

namespace engine::core::ecs { class World; }

namespace engine::editor {

class ColliderResizeCommand : public ICommand {
public:
    ColliderResizeCommand(core::ecs::World& world,
                          core::ecs::Entity entity,
                          const core::ColliderComponent& before,
                          const core::ColliderComponent& after);

    void        execute() override;
    void        undo()    override;
    const char* name()    const override { return "Resize Collider"; }

private:
    void apply(const core::ColliderComponent& c);

    core::ecs::World*       world_;
    core::ecs::Entity       entity_;
    core::ColliderComponent before_;
    core::ColliderComponent after_;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
