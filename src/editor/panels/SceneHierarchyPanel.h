#pragma once
#ifdef ENGINE_DEVREL

#include <core/ecs/Entity.h>
#include <core/ecs/EntityFactory.h>

#include <filesystem>
#include <functional>

namespace engine::core::ecs { class World; }

namespace engine::editor {

class UndoStack;

// Entity tree with selection, search filter, and a create/delete context menu.
// Mutations go through the UndoStack so they are undoable.
class SceneHierarchyPanel {
public:
    // Called when the user chooses "Save as Prefab..." from the context menu.
    using SaveAsPrefabFn = std::function<void(core::ecs::Entity, core::ecs::World&)>;

    void setSaveAsPrefabCallback(SaveAsPrefabFn fn) { onSaveAsPrefab_ = std::move(fn); }

    // Wires the entity factory and the editor's dirty flag so the Spawn menu
    // can instantiate archetypes and mark the scene modified.
    void setEntityFactory(core::ecs::EntityFactory* factory, bool* dirtyFlag) {
        entityFactory_ = factory;
        sceneDirty_    = dirtyFlag;
    }

    // Returns the (possibly changed) selection. Pass the current selection in.
    core::ecs::Entity draw(core::ecs::World& world,
                           core::ecs::Entity selected,
                           UndoStack& undo,
                           bool* open);

private:
    void drawEntityNode(core::ecs::World& world,
                        core::ecs::Entity entity,
                        core::ecs::Entity& newSelection,
                        core::ecs::Entity& toDelete,
                        core::ecs::Entity& saveAsPrefabEntity,
                        int depth);

    char                         searchBuffer_[128] = {};
    SaveAsPrefabFn               onSaveAsPrefab_;
    core::ecs::EntityFactory*    entityFactory_ = nullptr;
    bool*                        sceneDirty_    = nullptr;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
