#ifdef ENGINE_DEVREL

#include "editor/panels/InspectorPanel.h"

#include <core/ecs/World.h>
#include <core/diag/Assert.h>

#include <cstdio>

namespace engine::editor {

void ComponentEditorRegistry::registerWidget(core::ecs::ComponentTypeId id, Widget widget) {
    widgets_[id] = std::move(widget);
}

const ComponentEditorRegistry::Widget* ComponentEditorRegistry::find(
    core::ecs::ComponentTypeId id) const {
    return widgets_[id] ? &widgets_[id] : nullptr;
}

bool ComponentEditorRegistry::hasWidget(core::ecs::ComponentTypeId id) const noexcept {
    return static_cast<bool>(widgets_[id]);
}

void ComponentEditorRegistry::validateCoverage() const {
#if !defined(NDEBUG)
    for (uint16_t id = 0; id < 256; ++id) {
        const auto cid = static_cast<core::ecs::ComponentTypeId>(id);
        const auto& meta = core::ecs::World::getComponentMeta(cid);
        if (!meta.name) continue;
        if (hasWidget(cid)) continue;

        // Format a helpful message so the developer knows which component needs a widget
        // without having to attach a debugger.
        char msg[256];
        std::snprintf(msg, sizeof(msg),
            "Component %d ('%s') is registered in the ECS but has no editor widget. "
            "Call registerWidget (or registerTraits<T>) for it in "
            "EditorApp::registerComponentWidgets().",
            static_cast<int>(id), meta.name);
        core::diag::reportAssertionFailure(__FILE__, __LINE__, "hasWidget(cid)", msg);
        ENGINE_DEBUG_BREAK();
        std::abort();
    }
#endif
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
