#ifdef ENGINE_DEVREL

#include "editor/panels/InspectorPanel.h"

#include <core/ecs/World.h>
#include <core/ecs/Name.h>
#include <core/ecs/EditorContext.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>
#include <core/components/Lifetime.h>
#include <core/components/TeamTag.h>

#include <imgui.h>

namespace engine::editor {

void ComponentEditorRegistry::registerWidget(core::ecs::ComponentTypeId id, Widget widget) {
    widgets_[id] = std::move(widget);
}

const ComponentEditorRegistry::Widget* ComponentEditorRegistry::find(
    core::ecs::ComponentTypeId id) const {
    return widgets_[id] ? &widgets_[id] : nullptr;
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

void InspectorPanel::drawAddComponentMenu(core::ecs::World& world, core::ecs::Entity e) {
    if (ImGui::BeginPopup("##addcomp")) {
        addMenuItem<core::Transform>(world, e, "Transform");
        addMenuItem<core::Health>(world, e, "Health");
        addMenuItem<core::Lifetime>(world, e, "Lifetime");
        addMenuItem<core::TeamTag>(world, e, "TeamTag");
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

    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("##addcomp");
    }
    drawAddComponentMenu(world, selected);

    ImGui::Separator();

    // Components to remove are collected then applied after iteration so we do
    // not mutate the archetype while forEachComponentOnEntity is walking it.
    core::ecs::ComponentTypeId removeId = 0xFF;

    world.forEachComponentOnEntity(selected,
        [&](core::ecs::ComponentTypeId typeId, void* data) {
            const auto& meta = core::ecs::World::getComponentMeta(typeId);
            const char* header = meta.name ? meta.name : "Component";

            ImGui::PushID(static_cast<int>(typeId));
            const bool openHeader = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);

            // Name (id 0) and Transform (id 1) are core; don't offer removal.
            if (typeId != core::ecs::Name::kComponentId &&
                typeId != core::Transform::kComponentId) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 70.0f);
                if (ImGui::SmallButton("Remove")) {
                    removeId = typeId;
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
                }
            }
            ImGui::PopID();
        });

    if (removeId != 0xFF) {
        switch (removeId) {
            case core::Health::kComponentId:   world.removeComponent<core::Health>(selected);   break;
            case core::Lifetime::kComponentId: world.removeComponent<core::Lifetime>(selected); break;
            case core::TeamTag::kComponentId:  world.removeComponent<core::TeamTag>(selected);  break;
            default: break;
        }
    }

    ImGui::End();
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
