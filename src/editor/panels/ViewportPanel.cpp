#ifdef ENGINE_DEVREL

#include "editor/panels/ViewportPanel.h"
#include "editor/SelectionSystem.h"
#include "editor/UndoStack.h"
#include "editor/commands/TransformCommand.h"

#include <core/ecs/World.h>
#include <core/math/Quat.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace engine::editor {

using core::math::Vec3;
using core::math::Vec4;
using core::math::Mat4;

namespace {
constexpr float kPi = 3.14159265358979323846f;

// Project a world point to screen pixel coordinates relative to (originX,originY).
// Returns false if behind the camera.
bool worldToScreen(const Vec3& world, const Mat4& viewProj,
                   float originX, float originY, float w, float h,
                   ImVec2& outScreen) {
    Vec4 clip = Vec4{world, 1.0f} * viewProj;
    if (clip.w <= 0.0001f) return false;
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    outScreen.x = originX + (ndcX * 0.5f + 0.5f) * w;
    outScreen.y = originY + (1.0f - (ndcY * 0.5f + 0.5f)) * h;
    return true;
}

float snapTo(float value, float step) {
    if (step <= 0.0f) return value;
    return std::round(value / step) * step;
}

// Draws a world-space line segment with near-plane clipping so lines that
// straddle the camera plane still appear (rather than being silently dropped).
void drawClippedLine(ImDrawList* dl,
                     const Vec3& a, const Vec3& b,
                     const core::math::Mat4& viewProj,
                     float originX, float originY, float w, float h,
                     ImU32 col, float thickness = 1.0f) {
    Vec4 ca = Vec4{a, 1.0f} * viewProj;
    Vec4 cb = Vec4{b, 1.0f} * viewProj;

    constexpr float kNear = 0.0001f;
    if (ca.w <= kNear && cb.w <= kNear) return;

    if (ca.w <= kNear) {
        float t = (kNear - ca.w) / (cb.w - ca.w);
        ca.x += t * (cb.x - ca.x);
        ca.y += t * (cb.y - ca.y);
        ca.w  = kNear;
    } else if (cb.w <= kNear) {
        float t = (kNear - cb.w) / (ca.w - cb.w);
        cb.x += t * (ca.x - cb.x);
        cb.y += t * (ca.y - cb.y);
        cb.w  = kNear;
    }

    auto toScreen = [&](const Vec4& c) -> ImVec2 {
        float nx = c.x / c.w, ny = c.y / c.w;
        return { originX + (nx * 0.5f + 0.5f) * w,
                 originY + (1.0f - (ny * 0.5f + 0.5f)) * h };
    };

    dl->AddLine(toScreen(ca), toScreen(cb), col, thickness);
}
}

void ViewportPanel::handleCameraInput(EditorCamera& camera, bool hovered) {
    ImGuiIO& io = ImGui::GetIO();

    // Start camera drag only when right-click originates inside the viewport.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        cameraRightDragging_ = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        cameraRightDragging_ = false;

    EditorCamera::Input in{};
    in.rightMouseDown = cameraRightDragging_;
    in.altDown        = io.KeyAlt;
    in.mouseDeltaX    = io.MouseDelta.x;
    in.mouseDeltaY    = io.MouseDelta.y;

    if (hovered) {
        in.scrollDelta = io.MouseWheel;
        in.frameRequested = ImGui::IsKeyPressed(ImGuiKey_F, false);
    }

    if (in.rightMouseDown) {
        in.moveForward = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) -
                         (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
        in.moveRight   = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) -
                         (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
        in.moveUp      = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) -
                         (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
        in.sprint      = io.KeyShift;
    }

    camera.update(in, io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 60.0f);
}

void ViewportPanel::drawGizmo(core::ecs::World& world, core::ecs::Entity selected,
                              const Mat4& viewProj, UndoStack& undo) {
    auto* tr = world.tryGet<core::Transform>(selected);
    if (!tr) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin;
    if (!worldToScreen(tr->position, viewProj, originX_, originY_,
                       contentWidth_, contentHeight_, origin)) {
        return;
    }

    const ImU32 axisColX = IM_COL32(230, 70, 70, 255);
    const ImU32 axisColY = IM_COL32(70, 220, 70, 255);
    const ImU32 axisColZ = IM_COL32(80, 120, 240, 255);

    // Draw axis handles scaled in world space so they stay roughly constant size.
    const float handleLen = 1.0f;
    ImVec2 endX, endY, endZ;
    worldToScreen(tr->position + Vec3{handleLen, 0, 0}, viewProj, originX_, originY_, contentWidth_, contentHeight_, endX);
    worldToScreen(tr->position + Vec3{0, handleLen, 0}, viewProj, originX_, originY_, contentWidth_, contentHeight_, endY);
    worldToScreen(tr->position + Vec3{0, 0, handleLen}, viewProj, originX_, originY_, contentWidth_, contentHeight_, endZ);

    dl->AddLine(origin, endX, axisColX, 2.5f);
    dl->AddLine(origin, endY, axisColY, 2.5f);
    dl->AddLine(origin, endZ, axisColZ, 2.5f);
    dl->AddCircleFilled(origin, 4.0f, IM_COL32(240, 240, 240, 255));

    if (gizmoOp_ == GizmoOp::None) return;

    // Begin a drag with left mouse over the gizmo center; apply along screen axes.
    ImGuiIO& io = ImGui::GetIO();
    const bool overGizmo = (std::fabs(io.MousePos.x - origin.x) < 14.0f &&
                            std::fabs(io.MousePos.y - origin.y) < 14.0f);

    if (!dragging_ && overGizmo && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        dragging_  = true;
        dragStart_ = *tr;
    }

    if (dragging_) {
        const float dx = io.MouseDelta.x;
        const float dy = io.MouseDelta.y;
        const bool snap = snapEnabled_ || io.KeyCtrl;

        switch (gizmoOp_) {
            case GizmoOp::Translate: {
                // Map screen drag to world XZ plane plus vertical on Y.
                const float worldPerPixel = 0.01f;
                tr->position.x += dx * worldPerPixel;
                tr->position.y -= dy * worldPerPixel;
                if (snap) {
                    tr->position.x = snapTo(tr->position.x, snapTranslate_);
                    tr->position.y = snapTo(tr->position.y, snapTranslate_);
                    tr->position.z = snapTo(tr->position.z, snapTranslate_);
                }
                break;
            }
            case GizmoOp::Rotate: {
                const float angle = dx * 0.01f;
                core::math::Quat delta = core::math::fromAxisAngle(Vec3{0, 1, 0}, angle);
                tr->rotation = core::math::normalize(delta * tr->rotation);
                break;
            }
            case GizmoOp::Scale: {
                const float factor = 1.0f + dx * 0.005f;
                tr->scale = tr->scale * factor;
                if (snap) {
                    tr->scale.x = snapTo(tr->scale.x, snapScale_);
                    tr->scale.y = snapTo(tr->scale.y, snapScale_);
                    tr->scale.z = snapTo(tr->scale.z, snapScale_);
                }
                break;
            }
            case GizmoOp::None: break;
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            dragging_ = false;
            // Record the whole drag as one undoable command.
            undo.push(std::make_unique<TransformCommand>(world, selected, dragStart_, *tr));
        }
    }
}

void ViewportPanel::drawOverlays(core::ecs::World& world, const Mat4& viewProj) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (overlays_.grid) {
        const ImU32 gridCol = IM_COL32(120, 120, 120, 90);
        const int   half    = 10;
        for (int i = -half; i <= half; ++i) {
            const float f = static_cast<float>(i);
            const float e = static_cast<float>(half);
            drawClippedLine(dl, Vec3{f, 0, -e}, Vec3{f, 0,  e},
                            viewProj, originX_, originY_, contentWidth_, contentHeight_, gridCol);
            drawClippedLine(dl, Vec3{-e, 0, f}, Vec3{ e, 0, f},
                            viewProj, originX_, originY_, contentWidth_, contentHeight_, gridCol);
        }
    }

    if (overlays_.boundingBox) {
        const ImU32 bbCol = IM_COL32(255, 200, 60, 160);
        world.forEachEntity([&](core::ecs::Entity e) {
            auto* tr = world.tryGet<core::Transform>(e);
            if (!tr) return;
            // Unit cube around the entity origin scaled by its transform scale.
            const Vec3 c = tr->position;
            const Vec3 h = tr->scale * 0.5f;
            const Vec3 corners[8] = {
                c + Vec3{-h.x, -h.y, -h.z}, c + Vec3{ h.x, -h.y, -h.z},
                c + Vec3{ h.x,  h.y, -h.z}, c + Vec3{-h.x,  h.y, -h.z},
                c + Vec3{-h.x, -h.y,  h.z}, c + Vec3{ h.x, -h.y,  h.z},
                c + Vec3{ h.x,  h.y,  h.z}, c + Vec3{-h.x,  h.y,  h.z},
            };
            ImVec2 s[8];
            bool ok = true;
            for (int i = 0; i < 8; ++i) {
                ok &= worldToScreen(corners[i], viewProj, originX_, originY_,
                                    contentWidth_, contentHeight_, s[i]);
            }
            if (!ok) return;
            const int edges[12][2] = {
                {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
            };
            for (auto& ed : edges) dl->AddLine(s[ed[0]], s[ed[1]], bbCol, 1.0f);
        });
    }
}

void ViewportPanel::drawOrientationWidget(const core::math::Mat4& view) {
    constexpr float kSize   = 80.0f;
    constexpr float kMargin = 12.0f;
    constexpr float kArm    = kSize * 0.36f;
    constexpr float kDotR   = 4.5f;

    const float cx = originX_ + contentWidth_  - kMargin - kSize * 0.5f;
    const float cy = originY_ + contentHeight_ - kMargin - kSize * 0.5f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled({cx, cy}, kSize * 0.5f, IM_COL32(28, 28, 32, 160));

    struct AxisDesc { float x, y, z; ImU32 col; const char* label; };
    AxisDesc axes[3] = {
        { 1, 0, 0, IM_COL32(230,  70,  70, 255), "X" },
        { 0, 1, 0, IM_COL32( 70, 220,  70, 255), "Y" },
        { 0, 0, 1, IM_COL32( 80, 120, 240, 255), "Z" },
    };

    // Project a world direction through the view rotation (ignore translation).
    // Row-vector convention: result = dir * view (upper 3x3 only).
    // view-space x = dot(dir, column 0 of upper 3x3) = m[0][0]*dx + m[1][0]*dy + m[2][0]*dz
    auto project = [&](float dx, float dy, float dz) -> ImVec2 {
        float sx =   dx * view.m[0][0] + dy * view.m[1][0] + dz * view.m[2][0];
        float sy = -(dx * view.m[0][1] + dy * view.m[1][1] + dz * view.m[2][1]); // flip Y
        return { cx + sx * kArm, cy + sy * kArm };
    };

    // Sort by view-space Z: largest Z (furthest behind camera) drawn first.
    // view-space z = m[0][2]*dx + m[1][2]*dy + m[2][2]*dz
    int order[3] = {0, 1, 2};
    float depths[3];
    for (int i = 0; i < 3; ++i)
        depths[i] = axes[i].x * view.m[0][2] + axes[i].y * view.m[1][2] + axes[i].z * view.m[2][2];
    std::sort(order, order + 3, [&](int a, int b) { return depths[a] > depths[b]; });

    for (int oi = 0; oi < 3; ++oi) {
        const auto& ax  = axes[order[oi]];
        ImVec2 tip = project( ax.x,  ax.y,  ax.z);
        ImVec2 neg = project(-ax.x, -ax.y, -ax.z);

        // Negative half-axis — dim.
        ImU32 dimCol = (ax.col & 0x00FFFFFFu) | 0x55000000u;
        dl->AddLine({cx, cy}, neg, dimCol, 1.5f);

        dl->AddLine({cx, cy}, tip, ax.col, 2.5f);
        dl->AddCircleFilled(tip, kDotR, ax.col);
        dl->AddText({tip.x + 5.0f, tip.y - 7.0f}, ax.col, ax.label);
    }
}

void ViewportPanel::draw(core::ecs::World& world,
                         core::ecs::Entity& selected,
                         EditorCamera& camera,
                         SelectionSystem& picking,
                         UndoStack& undo,
                         uint64_t sceneTextureSrv,
                         bool* open) {
    if (open && !*open) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool visible = ImGui::Begin("Viewport", open);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }

    focused_ = ImGui::IsWindowFocused();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    contentWidth_  = avail.x > 1.0f ? avail.x : 1.0f;
    contentHeight_ = avail.y > 1.0f ? avail.y : 1.0f;
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    originX_ = cursor.x;
    originY_ = cursor.y;

    // Gizmo hotkeys (only when the viewport is focused).
    if (focused_) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) gizmoOp_ = GizmoOp::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) gizmoOp_ = GizmoOp::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) gizmoOp_ = GizmoOp::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) gizmoOp_ = GizmoOp::None;
    }

    // Draw the rendered scene texture (or a placeholder).
    if (sceneTextureSrv != 0) {
        ImGui::Image(static_cast<ImTextureID>(sceneTextureSrv),
                     ImVec2(contentWidth_, contentHeight_));
    } else {
        ImGui::Dummy(ImVec2(contentWidth_, contentHeight_));
        ImGui::GetWindowDrawList()->AddRectFilled(
            cursor, ImVec2(cursor.x + contentWidth_, cursor.y + contentHeight_),
            IM_COL32(30, 30, 34, 255));
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(cursor.x + 12, cursor.y + 12), IM_COL32(160, 160, 160, 255),
            "(scene render target unavailable)");
    }

    const bool hovered = ImGui::IsItemHovered();

    // Build view-projection for overlays/gizmo/picking.
    const float aspect = contentWidth_ / contentHeight_;
    const Mat4 view = camera.viewMatrix();
    const Mat4 proj = core::math::perspectiveRhYupReverseZ(
        camera.fovYDegrees * (kPi / 180.0f), aspect, camera.nearZ, camera.farZ);
    const Mat4 viewProj = view * proj;

    handleCameraInput(camera, hovered);

    // Left-click in empty space (not dragging gizmo, not orbiting) picks.
    if (hovered && !dragging_ && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // Rebuild pickables from the world (unit AABB per entity transform).
        std::vector<SelectionSystem::Pickable> picks;
        world.forEachEntity([&](core::ecs::Entity e) {
            if (auto* tr = world.tryGet<core::Transform>(e)) {
                core::math::AABB box = core::math::AABB::fromCenterExtents(
                    tr->position, tr->scale * 0.5f);
                picks.push_back({e, box});
            }
        });
        picking.setPickables(std::move(picks));

        const ImVec2 mp = ImGui::GetMousePos();
        core::ecs::Entity hit = picking.pickAtPixel(
            mp.x - originX_, mp.y - originY_, contentWidth_, contentHeight_, viewProj);
        if (world.isAlive(hit)) selected = hit;
    }

    drawOverlays(world, viewProj);
    if (world.isAlive(selected)) {
        drawGizmo(world, selected, viewProj, undo);
    }
    drawOrientationWidget(view);

    ImGui::End();
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
