#ifdef ENGINE_DEVREL

#include "editor/panels/SceneHierarchyPanel.h"
#include "editor/UndoStack.h"
#include "editor/commands/EntityCommand.h"

#include <core/ecs/World.h>
#include <core/ecs/Name.h>

#include <imgui.h>

#include <cctype>
#include <cstdio>
#include <memory>
#include <string>

namespace engine::editor {

namespace {
bool containsCI(const std::string& haystack, const char* needle) {
    if (needle[0] == '\0') return true;
    std::string h = haystack;
    std::string n = needle;
    for (char& c : h) c = static_cast<char>(std::tolower((unsigned char)c));
    for (char& c : n) c = static_cast<char>(std::tolower((unsigned char)c));
    return h.find(n) != std::string::npos;
}
}

core::ecs::Entity SceneHierarchyPanel::draw(core::ecs::World& world,
                                            core::ecs::Entity selected,
                                            UndoStack& undo,
                                            bool* open) {
    if (open && !*open) return selected;
    if (!ImGui::Begin("Hierarchy", open)) {
        ImGui::End();
        return selected;
    }

    if (ImGui::Button("+ Entity")) {
        auto cmd = std::make_unique<CreateEntityCommand>(world, "Entity");
        core::ecs::Entity created = core::ecs::kInvalidEntity;
        // We need the created handle after execution; push() executes immediately.
        CreateEntityCommand* raw = cmd.get();
        undo.push(std::move(cmd));
        created  = raw->entity();
        selected = created;
    }
    ImGui::SameLine();
    const bool canDelete = world.isAlive(selected);
    if (!canDelete) ImGui::BeginDisabled();
    if (ImGui::Button("- Entity") && canDelete) {
        undo.push(std::make_unique<DestroyEntityCommand>(world, selected));
        selected = core::ecs::kInvalidEntity;
    }
    if (!canDelete) ImGui::EndDisabled();

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##hsearch", "Search", searchBuffer_, sizeof(searchBuffer_));
    ImGui::Separator();

    core::ecs::Entity newSelection = selected;
    core::ecs::Entity toDelete     = core::ecs::kInvalidEntity;

    if (ImGui::BeginChild("##htree")) {
        world.forEachEntity([&](core::ecs::Entity e) {
            std::string label;
            if (auto* nm = world.tryGet<core::ecs::Name>(e); nm && nm->c_str()[0] != '\0') {
                label = nm->c_str();
            } else {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "Entity %u:%u", e.index, e.generation);
                label = buf;
            }

            if (!containsCI(label, searchBuffer_)) return;

            // Unique id per entity so same-named entities are independently selectable.
            ImGui::PushID(static_cast<int>(e.index));
            const bool isSel = (e == newSelection);
            if (ImGui::Selectable(label.c_str(), isSel)) {
                newSelection = e;
            }
            if (ImGui::BeginPopupContextItem("##ctx")) {
                newSelection = e;
                if (ImGui::MenuItem("Delete")) {
                    toDelete = e;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        });
    }
    ImGui::EndChild();

    if (world.isAlive(toDelete)) {
        undo.push(std::make_unique<DestroyEntityCommand>(world, toDelete));
        if (newSelection == toDelete) newSelection = core::ecs::kInvalidEntity;
    }

    ImGui::End();
    return newSelection;
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
