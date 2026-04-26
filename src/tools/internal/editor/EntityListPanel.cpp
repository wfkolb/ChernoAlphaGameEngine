#ifdef ENGINE_DEVREL

#include <imgui.h>
#include <format>

#include "tools/internal/editor/EntityListPanel.h"
#include <core/ecs/Name.h>

namespace engine::tools::internal {

engine::core::ecs::Entity drawEntityListPanel(
    engine::core::ecs::World& world,
    engine::core::ecs::Entity currentSelection)
{
    engine::core::ecs::Entity selected = currentSelection;

    ImGui::Begin("Entities");
    world.forEachEntity([&](engine::core::ecs::Entity entity) {
        const char* nameStr = nullptr;
        if (auto* nm = world.tryGet<engine::core::ecs::Name>(entity))
            nameStr = (nm->c_str()[0] != '\0') ? nm->c_str() : nullptr;

        std::string fallback;
        if (!nameStr) {
            fallback = std::format("Entity {}:{}", entity.index, entity.generation);
            nameStr  = fallback.c_str();
        }

        bool isSelected = (entity == selected);
        if (ImGui::Selectable(nameStr, isSelected))
            selected = entity;
    });
    ImGui::End();

    return selected;
}

} // namespace engine::tools::internal

#endif // ENGINE_DEVREL
