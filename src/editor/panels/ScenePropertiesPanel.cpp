#ifdef ENGINE_DEVREL

#include "editor/panels/ScenePropertiesPanel.h"

#include <core/scene/SceneGlobals.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string_view>

namespace engine::editor {

bool ScenePropertiesPanel::draw(core::scene::SceneGlobals& g, bool* open) {
    if (open && !*open) return false;
    if (!ImGui::Begin("Scene Properties", open)) {
        ImGui::End();
        return false;
    }

    bool changed = false;

    // ── Identity ────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Identity");

    char nameBuf[256] = {};
    const size_t copyLen = std::min(g.sceneName.size(), sizeof(nameBuf) - 1);
    g.sceneName.copy(nameBuf, copyLen);
    if (ImGui::InputText("Scene Name", nameBuf, sizeof(nameBuf))) {
        g.sceneName = nameBuf;
        changed = true;
    }

    ImGui::LabelText("Scene ID", "%u", g.sceneId);

    // ── Gameplay ────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Gameplay");

    char modeBuf[256] = {};
    const size_t modeLen = std::min(g.gameMode.size(), sizeof(modeBuf) - 1);
    g.gameMode.copy(modeBuf, modeLen);
    if (ImGui::InputText("Game Mode", modeBuf, sizeof(modeBuf))) {
        g.gameMode = modeBuf;
        changed = true;
    }

    {
        // Display as mm:ss but edit as total seconds.
        int totalSecs = static_cast<int>(g.matchTimeLimit);
        const int mm = totalSecs / 60;
        const int ss = totalSecs % 60;
        char timeBuf[16];
        std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mm, ss);
        ImGui::LabelText("Match Time (mm:ss)", "%s", timeBuf);
        if (ImGui::DragFloat("Match Time (s)", &g.matchTimeLimit, 10.f, 0.f, 3600.f))
            changed = true;
    }

    {
        int mp = g.maxPlayers;
        if (ImGui::DragInt("Max Players", &mp, 1, 1, 64)) {
            g.maxPlayers = mp;
            changed = true;
        }
    }

    // ── Physics ─────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Physics");

    float grav[3] = { g.gravity.x, g.gravity.y, g.gravity.z };
    if (ImGui::DragFloat3("Gravity (m/s²)", grav, 0.1f, -50.f, 50.f)) {
        g.gravity = { grav[0], grav[1], grav[2] };
        changed = true;
    }

    // ── Rendering ───────────────────────────────────────────────────────────
    ImGui::SeparatorText("Rendering");

    float ambient[3] = { g.ambientLight.x, g.ambientLight.y, g.ambientLight.z };
    if (ImGui::ColorEdit3("Ambient Light", ambient)) {
        g.ambientLight = { ambient[0], ambient[1], ambient[2] };
        changed = true;
    }

    float fogCol[3] = { g.fogColor.x, g.fogColor.y, g.fogColor.z };
    if (ImGui::ColorEdit3("Fog Color", fogCol)) {
        g.fogColor = { fogCol[0], fogCol[1], fogCol[2] };
        changed = true;
    }

    if (ImGui::DragFloat("Fog Density", &g.fogDensity, 0.001f, 0.f, 1.f))
        changed = true;

    // ── Skybox ──────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Skybox");

    {
        char skyBuf[512] = {};
        const size_t skyLen = std::min(g.skyboxAssetPath.size(), sizeof(skyBuf) - 1);
        g.skyboxAssetPath.copy(skyBuf, skyLen);
        if (ImGui::InputText("Skybox Asset", skyBuf, sizeof(skyBuf))) {
            g.skyboxAssetPath = skyBuf;
            changed = true;
        }
        // Drag-drop target: accept .easset drops onto the input field
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                const char* dropped = static_cast<const char*>(payload->Data);
                std::string_view sv(dropped, payload->DataSize);
                // Accept only .easset files
                if (sv.ends_with(".easset")) {
                    g.skyboxAssetPath = std::string(sv);
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    // ── Shadows ─────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Shadows");

    if (ImGui::DragFloat("Shadow Split \xce\xbb", &g.shadowSplitLambda, 0.01f, 0.0f, 1.0f,
                         "%.2f", ImGuiSliderFlags_AlwaysClamp))
        changed = true;

    ImGui::End();
    return changed;
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
