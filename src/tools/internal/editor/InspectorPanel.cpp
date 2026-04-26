#ifdef ENGINE_DEVREL

#include <imgui.h>

#include "tools/internal/editor/InspectorPanel.h"
#include <core/ecs/Name.h>
#include <core/ecs/EditorContext.h>

namespace engine::tools::internal {

void drawInspectorPanel(
    engine::core::ecs::World& world,
    engine::core::ecs::Entity entity)
{
    ImGui::Begin("Inspector");

    if (!world.isAlive(entity)) {
        ImGui::TextDisabled("No entity selected");
        ImGui::End();
        return;
    }

    {
        auto* nm = world.tryGet<engine::core::ecs::Name>(entity);
        if (nm && nm->c_str()[0] != '\0')
            ImGui::Text("%s", nm->c_str());
        else
            ImGui::Text("Entity %u (gen %u)", entity.index, entity.generation);
    }
    ImGui::Separator();

    world.forEachComponentOnEntity(entity,
        [](engine::core::ecs::ComponentTypeId typeId, void* data) {
            const auto& meta = engine::core::ecs::World::getComponentMeta(typeId);
            if (ImGui::CollapsingHeader(meta.name, ImGuiTreeNodeFlags_DefaultOpen)) {
                if (meta.inspect) {
                    engine::core::ecs::EditorContext ctx{};
                    meta.inspect(data, ctx);
                } else {
                    ImGui::TextDisabled("(no inspector)");
                }
            }
        });

    ImGui::End();
}

} // namespace engine::tools::internal

#endif // ENGINE_DEVREL
