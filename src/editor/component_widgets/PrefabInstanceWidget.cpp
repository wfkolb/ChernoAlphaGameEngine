#ifdef ENGINE_DEVREL

#include "editor/component_widgets/PrefabInstanceWidget.h"
#include <imgui.h>

namespace engine::editor {

bool drawPrefabInstanceWidget(core::ecs::PrefabInstance& instance) {
    bool changed = false;

    // Read-only path display.
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.8f, 0.3f));
    ImGui::InputText("##prefab_path", instance.sourcePrefabPath,
                     sizeof(instance.sourcePrefabPath),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("[PREFAB]");

    ImGui::Text("Overridden components: 0x%08X", instance.overriddenComponents);

    return changed;
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
