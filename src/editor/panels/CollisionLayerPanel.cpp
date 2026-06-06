#ifdef ENGINE_DEVREL

#include "editor/panels/CollisionLayerPanel.h"

#include <toml++/toml.hpp>
#include <imgui.h>
#include <core/log.h>

#include <cstring>
#include <fstream>

namespace engine::editor {

namespace {

struct LayerNames {
    char names[physics::kMaxPhysicsLayers][32];
    LayerNames() {
        for (int i = 0; i < physics::kMaxPhysicsLayers; ++i) {
            std::snprintf(names[i], sizeof(names[i]), "Layer%d", i);
        }
    }
};

void saveToFile(const physics::QueryFilter& filter,
                const LayerNames& ln,
                const std::string& path) {
    toml::array nameArr;
    for (int i = 0; i < physics::kMaxPhysicsLayers; ++i)
        nameArr.push_back(std::string(ln.names[i]));

    toml::array rowArr;
    for (int i = 0; i < physics::kMaxPhysicsLayers; ++i)
        rowArr.push_back(static_cast<int64_t>(filter.layerMask[static_cast<size_t>(i)]));

    toml::table layers;
    layers.insert_or_assign("names", nameArr);

    toml::table matrix;
    matrix.insert_or_assign("rows", rowArr);

    toml::table root;
    root.insert_or_assign("layers", layers);
    root.insert_or_assign("matrix", matrix);

    std::ofstream out(path);
    if (!out) {
        LOG_WARN("CollisionLayerPanel: cannot write to '{}'", path);
        return;
    }
    out << root;
}

} // anonymous namespace

void CollisionLayerPanel::draw(physics::QueryFilter& filter,
                               const std::string& tomlPath,
                               bool* open) {
    if (open && !*open) return;
    if (!ImGui::Begin("Collision Layers", open)) {
        ImGui::End();
        return;
    }

    static LayerNames ln;

    // ── Layer names ──────────────────────────────────────────────────────────
    ImGui::SeparatorText("Layer Names");
    for (int i = 0; i < physics::kMaxPhysicsLayers; ++i) {
        ImGui::PushID(i);
        char label[16];
        std::snprintf(label, sizeof(label), "Layer %d", i);
        ImGui::SetNextItemWidth(200.f);
        ImGui::InputText(label, ln.names[i], sizeof(ln.names[i]));
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Collision Matrix (row × column)");
    ImGui::TextDisabled("Checked = layers collide.");

    // ── Matrix ────────────────────────────────────────────────────────────────
    constexpr int N = physics::kMaxPhysicsLayers;

    if (ImGui::BeginTable("layerMatrix", N + 1,
        ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
        ImVec2(0, 340))) {
        // Header row with column labels (short)
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80.f);
        for (int col = 0; col < N; ++col) {
            ImGui::TableSetupColumn(ln.names[col], ImGuiTableColumnFlags_WidthFixed, 32.f);
        }
        ImGui::TableHeadersRow();

        for (int row = 0; row < N; ++row) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(ln.names[row]);
            for (int col = 0; col < N; ++col) {
                ImGui::TableSetColumnIndex(col + 1);
                ImGui::PushID(row * N + col);
                bool collides = filter.collides(
                    static_cast<uint8_t>(row),
                    static_cast<uint8_t>(col));
                if (ImGui::Checkbox("##c", &collides)) {
                    filter.setCollides(
                        static_cast<uint8_t>(row),
                        static_cast<uint8_t>(col),
                        collides);
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("Save")) {
        saveToFile(filter, ln, tomlPath);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tomlPath.c_str());

    ImGui::End();
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
