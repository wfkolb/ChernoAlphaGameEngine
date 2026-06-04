#pragma once
#ifdef ENGINE_DEVREL

#include <core/ecs/Entity.h>

#include <array>
#include <functional>

namespace engine::core::ecs { class World; }

namespace engine::editor {

// Maps a component type id to a custom inspector widget. When a component has
// no registered widget the panel falls back to ComponentMeta::inspect, and if
// that is also absent it shows a "(no inspector)" stub.
class ComponentEditorRegistry {
public:
    // widget receives the raw component pointer and returns true if it mutated
    // the component (so the editor can mark the scene dirty).
    using Widget = std::function<bool(void* component)>;

    void registerWidget(core::ecs::ComponentTypeId id, Widget widget);
    const Widget* find(core::ecs::ComponentTypeId id) const;

private:
    std::array<Widget, 256> widgets_{};
};

// Component inspector. Shows the selected entity's components, drives custom or
// default widgets, and offers Add/Remove component menus.
class InspectorPanel {
public:
    explicit InspectorPanel(ComponentEditorRegistry& registry) : registry_(&registry) {}

    void draw(core::ecs::World& world, core::ecs::Entity selected, bool* open);

private:
    void drawAddComponentMenu(core::ecs::World& world, core::ecs::Entity e);

    ComponentEditorRegistry* registry_;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
