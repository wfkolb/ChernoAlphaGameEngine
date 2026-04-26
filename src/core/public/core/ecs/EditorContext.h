#pragma once

namespace engine::core::ecs {

// Passed to ComponentMeta::inspect by the editor each frame.
// Inspect functions set modified = true when an ImGui widget changes the component value.
struct EditorContext {
    bool modified = false;
};

} // namespace engine::core::ecs
