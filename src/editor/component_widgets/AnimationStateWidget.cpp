#ifdef ENGINE_DEVREL

#include "editor/component_widgets/AnimationStateWidget.h"
#include <imgui.h>
#include <cstdio>

namespace engine::editor {

bool drawAnimationStateWidget(core::AnimationState& state) {
    bool changed = false;

    static const char* kClipNames[] = { "Idle","Walk","Run","Jump","Fall","Land" };
    const int numClips = static_cast<int>(core::AnimationState::Clip::Count);

    ImGui::BeginDisabled(true); // Runtime drives clip — read-only in editor
    int curClipIdx = static_cast<int>(state.currentClip);
    ImGui::Combo("Current Clip", &curClipIdx, kClipNames, numClips);
    char timeBuf[32];
    std::snprintf(timeBuf, sizeof(timeBuf), "%.3f s", state.clipTimeSeconds);
    ImGui::InputText("Clip Time", timeBuf, sizeof(timeBuf),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();

    if (ImGui::SliderFloat("Blend Weight", &state.blendWeight, 0.f, 1.f)) {
        changed = true;
    }

    ImGui::Text("Is Grounded: %s", state.isGrounded ? "true" : "false");

    ImGui::Separator();
    // Force-clip combo for PIE testing.
    int forceClipIdx = static_cast<int>(state.currentClip);
    if (ImGui::Combo("Force Clip (PIE)", &forceClipIdx, kClipNames, numClips)) {
        state.currentClip = static_cast<core::AnimationState::Clip>(forceClipIdx);
        changed = true;
    }

    return changed;
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
