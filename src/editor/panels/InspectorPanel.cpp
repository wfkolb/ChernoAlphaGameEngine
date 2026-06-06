#ifdef ENGINE_DEVREL

#include "editor/panels/InspectorPanel.h"
#include "editor/component_widgets/ColliderWidget.h"
#include "editor/component_widgets/AnimationStateWidget.h"
#include "editor/component_widgets/PrefabInstanceWidget.h"

#include <core/ecs/World.h>
#include <core/ecs/Name.h>
#include <core/ecs/EditorContext.h>
#include <core/ecs/HierarchyComponent.h>
#include <core/ecs/PrefabInstance.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>
#include <core/components/Lifetime.h>
#include <core/components/TeamTag.h>
#include <core/components/ColliderComponent.h>
#include <core/components/AnimationState.h>
#include <core/components/MeshHandle.h>
#include <core/log.h>
#include <tools/PrefabSerializer.h>

#include <imgui.h>

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

namespace {
// Add-component helpers for the well-known component set. Generic add-by-id is
// not possible because World::addComponent is templated, so the menu enumerates
// the concrete types the editor knows how to author.
template <typename T>
void addMenuItem(core::ecs::World& world, core::ecs::Entity e, const char* label) {
    const bool has = world.hasComponent(e, T::kComponentId);
    if (has) ImGui::BeginDisabled();
    if (ImGui::MenuItem(label)) {
        world.addComponent<T>(e, T{});
    }
    if (has) ImGui::EndDisabled();
}

template <typename T>
bool removeButton(core::ecs::World& world, core::ecs::Entity e) {
    if (ImGui::SmallButton("Remove")) {
        world.removeComponent<T>(e);
        return true;
    }
    return false;
}
}

void InspectorPanel::revertComponentToPrefab(
    core::ecs::World& world,
    core::ecs::Entity entity,
    const core::ecs::PrefabInstance& pi,
    core::ecs::ComponentTypeId componentId)
{
    auto result = tools::PrefabSerializer::load(pi.sourcePrefabPath);
    if (!result) {
        LOG_WARN("InspectorPanel: cannot revert component {} — prefab '{}' failed to load",
                 componentId, pi.sourcePrefabPath);
        return;
    }

    if (result->entities.empty()) {
        LOG_WARN("InspectorPanel: cannot revert component {} — prefab '{}' has no entities",
                 componentId, pi.sourcePrefabPath);
        return;
    }

    const tools::PrefabSerializer::EntitySnapshot& root = result->entities[0];
    for (const tools::PrefabSerializer::ComponentData& cd : root.components) {
        if (cd.typeId == componentId) {
            world.addComponentRaw(entity, componentId, cd.bytes.data(), cd.bytes.size());
            return;
        }
    }

    LOG_WARN("InspectorPanel: cannot revert component {} — not found in prefab '{}'",
             componentId, pi.sourcePrefabPath);
}

void InspectorPanel::drawAddComponentMenu(core::ecs::World& world, core::ecs::Entity e) {
    if (ImGui::BeginPopup("##addcomp")) {
        addMenuItem<core::Transform>(world, e, "Transform");
        addMenuItem<core::Health>(world, e, "Health");
        addMenuItem<core::Lifetime>(world, e, "Lifetime");
        addMenuItem<core::TeamTag>(world, e, "TeamTag");
        addMenuItem<core::ColliderComponent>(world, e, "Collider");
        addMenuItem<core::AnimationState>(world, e, "AnimationState");
        addMenuItem<core::MeshHandle>(world, e, "MeshHandle");
        addMenuItem<core::ecs::HierarchyComponent>(world, e, "HierarchyComponent");
        ImGui::EndPopup();
    }
}

void InspectorPanel::draw(core::ecs::World& world, core::ecs::Entity selected, bool* open) {
    if (open && !*open) return;
    if (!ImGui::Begin("Inspector", open)) {
        ImGui::End();
        return;
    }

    if (!world.isAlive(selected)) {
        ImGui::TextDisabled("No entity selected");
        ImGui::End();
        return;
    }

    if (auto* nm = world.tryGet<core::ecs::Name>(selected); nm && nm->c_str()[0] != '\0') {
        ImGui::Text("%s", nm->c_str());
    } else {
        ImGui::Text("Entity %u (gen %u)", selected.index, selected.generation);
    }

    core::ecs::PrefabInstance* pi = world.tryGet<core::ecs::PrefabInstance>(selected);
    if (pi) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::Text("[PREFAB] %s", pi->sourcePrefabPath);
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("##addcomp");
    }
    drawAddComponentMenu(world, selected);

    ImGui::Separator();

    // Components to remove are collected then applied after iteration so we do
    // not mutate the archetype while forEachComponentOnEntity is walking it.
    core::ecs::ComponentTypeId removeId = 0xFF;
    core::ecs::ComponentTypeId revertId = 0xFF;

    world.forEachComponentOnEntity(selected,
        [&](core::ecs::ComponentTypeId typeId, void* data) {
            const auto& meta = core::ecs::World::getComponentMeta(typeId);
            const char* header = meta.name ? meta.name : "Component";

            const bool isOverridden = pi &&
                (pi->overriddenComponents & (1u << typeId));

            ImGui::PushID(static_cast<int>(typeId));

            if (isOverridden) {
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(p.x - 4.0f, p.y),
                    ImVec2(p.x - 4.0f, p.y + ImGui::GetFrameHeight()),
                    IM_COL32(50, 130, 255, 255), 2.0f);
            }

            const bool openHeader = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);

            // Name (id 0) and Transform (id 1) are core; don't offer removal.
            if (typeId != core::ecs::Name::kComponentId &&
                typeId != core::Transform::kComponentId) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 70.0f);
                if (ImGui::SmallButton("Remove")) {
                    removeId = typeId;
                }
            }

            if (isOverridden) {
                ImGui::SameLine();
                if (ImGui::SmallButton(reinterpret_cast<const char*>(u8"↺"))) {
                    revertId = typeId;
                }
            }

            if (openHeader) {
                if (const auto* widget = registry_->find(typeId)) {
                    (*widget)(data);
                } else if (meta.inspect) {
                    core::ecs::EditorContext ctx{};
                    meta.inspect(data, ctx);
                } else {
                    ImGui::TextDisabled("(no inspector)");
                    LOG_WARN("InspectorPanel: component type {} ('{}') has no registered "
                             "editor widget and no meta.inspect. Add a widget via "
                             "ComponentEditorRegistry::registerWidget() in EditorApp::registerComponentWidgets().",
                             typeId, header);
                }
            }
            ImGui::PopID();
        });

    if (removeId != 0xFF) {
        switch (removeId) {
            case core::Health::kComponentId:
                world.removeComponent<core::Health>(selected);            break;
            case core::Lifetime::kComponentId:
                world.removeComponent<core::Lifetime>(selected);          break;
            case core::TeamTag::kComponentId:
                world.removeComponent<core::TeamTag>(selected);           break;
            case core::ColliderComponent::kComponentId:
                world.removeComponent<core::ColliderComponent>(selected); break;
            case core::AnimationState::kComponentId:
                world.removeComponent<core::AnimationState>(selected);    break;
            case core::MeshHandle::kComponentId:
                world.removeComponent<core::MeshHandle>(selected);        break;
            case core::ecs::HierarchyComponent::kComponentId:
                world.removeComponent<core::ecs::HierarchyComponent>(selected); break;
            default: break;
        }
    }

    if (revertId != 0xFF && pi) {
        revertComponentToPrefab(world, selected, *pi, revertId);
        pi->overriddenComponents &= ~(1u << revertId);
    }

    ImGui::End();
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
