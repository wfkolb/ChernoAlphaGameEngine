#pragma once
#ifdef ENGINE_DEVREL

#include "editor/UndoStack.h"

#include <core/ecs/Entity.h>
#include <core/components/Transform.h>

namespace engine::core::ecs { class World; }

namespace engine::editor {

// Restores a captured before/after Transform on an entity. The editor captures
// `before` when a gizmo drag begins and `after` when it ends, then pushes one
// command so the whole drag is a single undo step.
class TransformCommand : public ICommand {
public:
    TransformCommand(core::ecs::World& world,
                     core::ecs::Entity entity,
                     const core::Transform& before,
                     const core::Transform& after);

    void execute() override;
    void undo() override;
    const char* name() const override { return "Transform"; }

private:
    void apply(const core::Transform& t);

    core::ecs::World*  world_;
    core::ecs::Entity  entity_;
    core::Transform    before_;
    core::Transform    after_;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
