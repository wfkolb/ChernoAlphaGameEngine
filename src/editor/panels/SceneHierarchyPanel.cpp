#ifdef ENGINE_DEVREL

#include "editor/panels/SceneHierarchyPanel.h"
#include "editor/SelectionSystem.h"
#include "editor/UndoStack.h"
#include "editor/commands/EntityCommand.h"

#include <core/ecs/World.h>
#include <core/ecs/Name.h>
#include <core/ecs/HierarchyComponent.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

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

std::string entityLabel(const core::ecs::World& world, core::ecs::Entity e) {
    if (const auto* nm = const_cast<core::ecs::World&>(world).tryGet<core::ecs::Name>(e);
        nm && nm->c_str()[0] != '\0') {
        return nm->c_str();
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Entity %u:%u", e.index, e.generation);
    return buf;
}
} // namespace

void SceneHierarchyPanel::drawEntityNode(
    core::ecs::World&     world,
    core::ecs::Entity     entity,
    core::ecs::Entity&    newSelection,
    core::ecs::Entity&    toDelete,
    core::ecs::Entity&    saveAsPrefabEntity,
    int                   depth)
{
    const std::string label = entityLabel(world, entity);

    if (!containsCI(label, searchBuffer_)) {
        // Still recurse into children in case a child matches.
        const auto* hc = world.tryGet<core::ecs::HierarchyComponent>(entity);
        if (hc) {
            core::ecs::Entity child = hc->firstChild;
            while (child != core::ecs::kInvalidEntity) {
                drawEntityNode(world, child, newSelection, toDelete, saveAsPrefabEntity, depth + 1);
                const auto* chc = world.tryGet<core::ecs::HierarchyComponent>(child);
                child = chc ? chc->nextSibling : core::ecs::kInvalidEntity;
            }
        }
        return;
    }

    // Track visit order for Shift+click range-select.
    visitOrder_.push_back(entity);

    ImGui::PushID(static_cast<int>(entity.index));

    const auto* hc         = world.tryGet<core::ecs::HierarchyComponent>(entity);
    const bool  hasChildren = hc && hc->firstChild != core::ecs::kInvalidEntity;

    // Highlight if it's the primary selection or part of multi-select.
    const bool isSel = (entity == newSelection) ||
                       (selectionSystem_ && selectionSystem_->isSelected(entity));

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (isSel)        flags |= ImGuiTreeNodeFlags_Selected;

    const bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        ImGuiIO& io = ImGui::GetIO();
        if (selectionSystem_) {
            if (io.KeyCtrl) {
                // Ctrl+click: toggle in multi-select.
                selectionSystem_->toggleSelect(entity);
                // Keep newSelection pointing at the last toggled entity when adding.
                if (selectionSystem_->isSelected(entity)) newSelection = entity;
            } else if (io.KeyShift) {
                // Shift+click: range-select using current visit order.
                selectionSystem_->rangeSelect(entity, visitOrder_);
                newSelection = entity;
            } else {
                // Plain click: single-select, clear multi.
                selectionSystem_->selectOnly(entity);
                newSelection = entity;
            }
        } else {
            newSelection = entity;
        }
    }

    if (ImGui::BeginPopupContextItem("##ctx")) {
        newSelection = entity;
        if (ImGui::MenuItem("Delete")) {
            toDelete = entity;
        }
        if (onSaveAsPrefab_ && ImGui::MenuItem("Save as Prefab...")) {
            saveAsPrefabEntity = entity;
        }
        ImGui::EndPopup();
    }

    if (hasChildren && nodeOpen) {
        core::ecs::Entity child = hc->firstChild;
        while (child != core::ecs::kInvalidEntity) {
            drawEntityNode(world, child, newSelection, toDelete, saveAsPrefabEntity, depth + 1);
            const auto* chc = world.tryGet<core::ecs::HierarchyComponent>(child);
            child = chc ? chc->nextSibling : core::ecs::kInvalidEntity;
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
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
        CreateEntityCommand* raw = cmd.get();
        undo.push(std::move(cmd));
        selected = raw->entity();
        if (selectionSystem_) selectionSystem_->selectOnly(selected);
    }
    ImGui::SameLine();
    const bool canDelete = world.isAlive(selected);
    if (!canDelete) ImGui::BeginDisabled();
    if (ImGui::Button("- Entity") && canDelete) {
        undo.push(std::make_unique<DestroyEntityCommand>(world, selected));
        selected = core::ecs::kInvalidEntity;
        if (selectionSystem_) selectionSystem_->clearSelection();
    }
    if (!canDelete) ImGui::EndDisabled();

    if (entityFactory_ && ImGui::BeginMenu("Spawn")) {
        if (ImGui::MenuItem("FpsCharacter")) {
            core::ecs::SpawnParams params{};
            const core::ecs::Entity spawned =
                entityFactory_->spawn("FpsCharacter", params, world);
            if (spawned != core::ecs::kInvalidEntity) {
                selected = spawned;
                if (selectionSystem_) selectionSystem_->selectOnly(selected);
                if (sceneDirty_) *sceneDirty_ = true;
            }
        }
        if (ImGui::MenuItem("SpawnPoint")) {
            core::ecs::SpawnParams params{};
            const core::ecs::Entity spawned =
                entityFactory_->spawn("SpawnPointEntity", params, world);
            if (spawned != core::ecs::kInvalidEntity) {
                selected = spawned;
                if (selectionSystem_) selectionSystem_->selectOnly(selected);
                if (sceneDirty_) *sceneDirty_ = true;
            }
        }
        ImGui::EndMenu();
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##hsearch", "Search", searchBuffer_, sizeof(searchBuffer_));
    ImGui::Separator();

    // Ctrl+D: duplicate all selected entities (or just the primary selection).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_D, false) &&
        ImGui::GetIO().KeyCtrl) {

        // Collect entities to duplicate (multi-select if available, else single).
        std::vector<core::ecs::Entity> toDup;
        if (selectionSystem_ && !selectionSystem_->selection().empty()) {
            toDup = selectionSystem_->selection();
        } else if (world.isAlive(selected)) {
            toDup.push_back(selected);
        }

        core::ecs::Entity lastDup = core::ecs::kInvalidEntity;
        for (core::ecs::Entity src : toDup) {
            if (!world.isAlive(src)) continue;
            auto cmd = std::make_unique<DuplicateEntityCommand>(world, src);
            DuplicateEntityCommand* raw = cmd.get();
            undo.push(std::move(cmd));
            if (raw->duplicate() != core::ecs::kInvalidEntity) {
                lastDup = raw->duplicate();
            }
        }

        if (lastDup != core::ecs::kInvalidEntity) {
            selected = lastDup;
            if (selectionSystem_) selectionSystem_->selectOnly(lastDup);
            if (sceneDirty_) *sceneDirty_ = true;
        }
    }

    // Reset visit order before each tree traversal (used by range-select).
    visitOrder_.clear();

    core::ecs::Entity newSelection        = selected;
    core::ecs::Entity toDelete            = core::ecs::kInvalidEntity;
    core::ecs::Entity saveAsPrefabEntity  = core::ecs::kInvalidEntity;

    if (ImGui::BeginChild("##htree")) {
        world.forEachEntity([&](core::ecs::Entity e) {
            // Only draw root entities here; children are drawn by drawEntityNode.
            const auto* hc = world.tryGet<core::ecs::HierarchyComponent>(e);
            if (hc && hc->parent != core::ecs::kInvalidEntity) return;
            drawEntityNode(world, e, newSelection, toDelete, saveAsPrefabEntity, 0);
        });
    }
    ImGui::EndChild();

    if (world.isAlive(toDelete)) {
        undo.push(std::make_unique<DestroyEntityCommand>(world, toDelete));
        if (newSelection == toDelete) {
            newSelection = core::ecs::kInvalidEntity;
        }
        if (selectionSystem_) selectionSystem_->clearSelection();
    }

    if (world.isAlive(saveAsPrefabEntity) && onSaveAsPrefab_) {
        onSaveAsPrefab_(saveAsPrefabEntity, world);
    }

    ImGui::End();
    return newSelection;
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
