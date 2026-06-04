#pragma once
#ifdef ENGINE_DEVREL

#include <core/ecs/Entity.h>

namespace engine::core::ecs { class World; }

namespace engine::editor {

class UndoStack;

// Entity tree with selection, search filter, and a create/delete context menu.
// Mutations go through the UndoStack so they are undoable.
class SceneHierarchyPanel {
public:
    // Returns the (possibly changed) selection. Pass the current selection in.
    core::ecs::Entity draw(core::ecs::World& world,
                           core::ecs::Entity selected,
                           UndoStack& undo,
                           bool* open);

private:
    char searchBuffer_[128] = {};
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
