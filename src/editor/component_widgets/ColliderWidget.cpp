#ifdef ENGINE_DEVREL

#include "editor/component_widgets/ColliderWidget.h"

#include <core/components/ColliderComponent.h>

#include <imgui.h>

#include <algorithm>

namespace engine::editor {

bool drawColliderWidget(core::ColliderComponent& c) {
    bool changed = false;

    // ── Enable / disable toggle ──────────────────────────────────────────────
    if (ImGui::Checkbox("Enabled", &c.enabled))
        changed = true;

    ImGui::Separator();

    // ── Shape selector ───────────────────────────────────────────────────────
    static const char* kShapeNames[] = { "Box", "Sphere", "Capsule" };
    int shapeIdx = static_cast<int>(c.shape);
    if (ImGui::Combo("Shape", &shapeIdx, kShapeNames, 3)) {
        const auto newShape = static_cast<core::ColliderComponent::Shape>(shapeIdx);
        if (newShape != c.shape) {
            c.shape = newShape;
            // Reset to defaults when switching shape type.
            switch (c.shape) {
                case core::ColliderComponent::Shape::Box:
                    c.params.box = {};
                    break;
                case core::ColliderComponent::Shape::Sphere:
                    c.params.sphere = {};
                    break;
                case core::ColliderComponent::Shape::Capsule:
                    c.params.capsule = {};
                    break;
            }
            changed = true;
        }
    }

    ImGui::Separator();

    // ── Per-shape parameters ─────────────────────────────────────────────────
    switch (c.shape) {
        case core::ColliderComponent::Shape::Box: {
            float he[3] = { c.params.box.halfX, c.params.box.halfY, c.params.box.halfZ };
            if (ImGui::DragFloat3("Half Extents (m)", he, 0.01f, 0.001f, 100.f)) {
                c.params.box.halfX = std::max(he[0], 0.001f);
                c.params.box.halfY = std::max(he[1], 0.001f);
                c.params.box.halfZ = std::max(he[2], 0.001f);
                changed = true;
            }
            break;
        }
        case core::ColliderComponent::Shape::Sphere: {
            if (ImGui::DragFloat("Radius (m)", &c.params.sphere.radius, 0.01f, 0.001f, 100.f)) {
                c.params.sphere.radius = std::max(c.params.sphere.radius, 0.001f);
                changed = true;
            }
            break;
        }
        case core::ColliderComponent::Shape::Capsule: {
            if (ImGui::DragFloat("Radius (m)", &c.params.capsule.radius, 0.01f, 0.001f, 100.f)) {
                c.params.capsule.radius = std::max(c.params.capsule.radius, 0.001f);
                changed = true;
            }
            if (ImGui::DragFloat("Half Height (m)", &c.params.capsule.halfHeight, 0.01f, 0.001f, 100.f)) {
                c.params.capsule.halfHeight = std::max(c.params.capsule.halfHeight, 0.001f);
                changed = true;
            }
            {
                const float total = c.params.capsule.radius * 2.f + c.params.capsule.halfHeight * 2.f;
                ImGui::LabelText("Total Height (m)", "%.3f", total);
            }
            break;
        }
    }

    ImGui::Separator();

    // ── Common fields ────────────────────────────────────────────────────────
    float offset[3] = { c.offsetX, c.offsetY, c.offsetZ };
    if (ImGui::DragFloat3("Local Offset (m)", offset, 0.01f)) {
        c.offsetX = offset[0];
        c.offsetY = offset[1];
        c.offsetZ = offset[2];
        changed = true;
    }

    {
        int layer = c.layerIndex;
        if (ImGui::DragInt("Layer", &layer, 1, 0, 15)) {
            c.layerIndex = static_cast<uint8_t>(std::clamp(layer, 0, 15));
            changed = true;
        }
    }

    {
        int mat = c.materialIndex;
        if (ImGui::DragInt("Material Index", &mat, 1, 0, 255)) {
            c.materialIndex = static_cast<uint8_t>(std::clamp(mat, 0, 255));
            changed = true;
        }
    }

    if (ImGui::Checkbox("Is Trigger", &c.isTrigger))
        changed = true;

    return changed;
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
