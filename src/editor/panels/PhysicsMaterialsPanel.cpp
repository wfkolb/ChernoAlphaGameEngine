#ifdef ENGINE_DEVREL

#include "editor/panels/PhysicsMaterialsPanel.h"

#include <toml++/toml.hpp>
#include <imgui.h>
#include <core/log.h>

#include <algorithm>
#include <fstream>
#include <vector>

namespace engine::editor {

namespace {

struct EditableMaterial {
    char  name[64]   = "default";
    float friction   = 0.5f;
    float restitution= 0.1f;
};

// Sync from live table to editable list.
void fromTable(const physics::PhysicsMaterialTable& tbl,
               std::vector<EditableMaterial>& list) {
    list.clear();
    const uint8_t n = tbl.count();
    for (uint8_t i = 0; i < n; ++i) {
        const auto& m = tbl.get(i);
        EditableMaterial e;
        const size_t cpLen = std::min(m.name.size(), sizeof(e.name) - 1);
        m.name.copy(e.name, cpLen);
        e.friction    = m.friction;
        e.restitution = m.restitution;
        list.push_back(e);
    }
    if (list.empty()) list.push_back({});
}

bool saveMaterials(const std::vector<EditableMaterial>& list,
                   const std::string& path,
                   physics::PhysicsMaterialTable& table) {
    toml::array arr;
    for (const auto& m : list) {
        toml::table t;
        t.insert_or_assign("name",        std::string(m.name));
        t.insert_or_assign("friction",    static_cast<double>(m.friction));
        t.insert_or_assign("restitution", static_cast<double>(m.restitution));
        arr.push_back(t);
    }
    toml::table root;
    root.insert_or_assign("material", arr);

    std::ofstream out(path);
    if (!out) {
        LOG_WARN("PhysicsMaterialsPanel: cannot write to '{}'", path);
        return false;
    }
    out << root;
    out.close();
    table.load(path);
    return true;
}

} // anonymous namespace

void PhysicsMaterialsPanel::draw(physics::PhysicsMaterialTable& table,
                                 const std::string& tomlPath,
                                 bool* open) {
    if (open && !*open) return;
    if (!ImGui::Begin("Physics Materials", open)) {
        ImGui::End();
        return;
    }

    static std::vector<EditableMaterial> materials;
    static bool loaded = false;
    if (!loaded) {
        fromTable(table, materials);
        loaded = true;
    }

    // ── Toolbar ──────────────────────────────────────────────────────────────
    if (ImGui::Button("+ Add")) {
        EditableMaterial m;
        materials.push_back(m);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload from disk")) {
        fromTable(table, materials);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        saveMaterials(materials, tomlPath, table);
    }
    ImGui::Separator();

    // ── Table ────────────────────────────────────────────────────────────────
    if (ImGui::BeginTable("mats", 5,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
        ImVec2(0, 0))) {
        ImGui::TableSetupColumn("#",          ImGuiTableColumnFlags_WidthFixed, 30.f);
        ImGui::TableSetupColumn("Name",       ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Friction",   ImGuiTableColumnFlags_WidthFixed, 80.f);
        ImGui::TableSetupColumn("Restitution",ImGuiTableColumnFlags_WidthFixed, 90.f);
        ImGui::TableSetupColumn("Remove",     ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableHeadersRow();

        int toRemove = -1;
        for (int i = 0; i < static_cast<int>(materials.size()); ++i) {
            auto& m = materials[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", i);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.f);
            ImGui::InputText("##n", m.name, sizeof(m.name));

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1.f);
            ImGui::DragFloat("##f", &m.friction, 0.01f, 0.f, 1.f, "%.2f");

            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-1.f);
            ImGui::DragFloat("##r", &m.restitution, 0.01f, 0.f, 1.f, "%.2f");

            ImGui::TableSetColumnIndex(4);
            if (i > 0) { // don't allow removing index 0 (default)
                if (ImGui::SmallButton("Del")) toRemove = i;
            } else {
                ImGui::TextDisabled("(default)");
            }

            ImGui::PopID();
        }

        if (toRemove >= 0)
            materials.erase(materials.begin() + toRemove);

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
