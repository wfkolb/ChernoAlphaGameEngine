#ifdef ENGINE_DEVREL

#include "editor/panels/ViewportPanel.h"
#include "editor/SelectionSystem.h"
#include "editor/UndoStack.h"
#include "editor/commands/ColliderResizeCommand.h"
#include "editor/commands/TransformCommand.h"

#include <core/ecs/World.h>
#include <core/ecs/Name.h>
#include <core/ecs/PrefabInstance.h>
#include <core/ecs/View.h>
#include <core/ecs/HierarchyComponent.h>
#include <core/components/ColliderComponent.h>
#include <core/components/Transform.h>
#include <core/components/SpawnPointComponent.h>
#include <core/components/TriggerComponent.h>
#include <rendering/Camera.h>
#include <core/math/Quat.h>
#include <physics/PhysicsWorld.h>
#include <tools/PrefabSerializer.h>
#include <tools/EassetLoader.h>
#include <core/components/MeshHandle.h>

#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
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

    // During PIE the player entity's Transform drives the view matrix; suppress
    // editor camera movement so the camera doesn't drift while playing.
    if (!pieActive_) {
        camera.update(in, io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 60.0f);
    }
}

void ViewportPanel::drawGizmo(core::ecs::World& world, core::ecs::Entity selected,
                              const Mat4& view, const Mat4& proj, UndoStack& undo) {
    (void)undo; // reserved for undo command on drag-end
    auto* tr = world.tryGet<core::Transform>(selected);
    if (!tr) return;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(originX_, originY_, contentWidth_, contentHeight_);

    // Determine whether this entity is a child — gizmo always operates in world space.
    const auto* hc = world.tryGet<core::ecs::HierarchyComponent>(selected);
    const bool hasParent = hc && hc->parent != core::ecs::kInvalidEntity;

    // Resolve the world-space TRS used to position the gizmo.
    core::math::Vec3 worldPos;
    core::math::Quat worldRot;
    core::math::Vec3 worldScl;
    if (hasParent) {
        const core::Transform wt = core::ecs::computeWorldTransform(world, selected);
        worldPos = wt.position;
        worldRot = wt.rotation;
        worldScl = wt.scale;
    } else {
        worldPos = tr->position;
        worldRot = tr->rotation;
        worldScl = tr->scale;
    }

    // Build a column-major TRS matrix from the world-space transform.
    // ImGuizmo expects column-major float[16] with column-vector convention (v' = M*v).
    // Engine uses row-major / row-vector convention (v' = v*M), so M_imgui = M_engine^T.
    // Transpose: put engine element [r][c] into ImGuizmo position [c*4+r] as element [c][r],
    // i.e. index the engine matrix with swapped indices: rotMat.m[c][r].
    const core::math::Mat4 rotMat = core::math::toMat4(worldRot);
    float matrix[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            matrix[c * 4 + r] = (r < 3 && c < 3) ? rotMat.m[c][r] :
                                 (r == 3 && c < 3) ? 0.0f :
                                 (r < 3 && c == 3) ? (&worldPos.x)[r] :
                                 (r == 3 && c == 3) ? 1.0f : 0.0f;
    // Scale columns 0-2 by world scale.
    matrix[0]  *= worldScl.x; matrix[1]  *= worldScl.x; matrix[2]  *= worldScl.x;
    matrix[4]  *= worldScl.y; matrix[5]  *= worldScl.y; matrix[6]  *= worldScl.y;
    matrix[8]  *= worldScl.z; matrix[9]  *= worldScl.z; matrix[10] *= worldScl.z;

    // Build column-major view and proj for ImGuizmo (transpose: m[c][r] not m[r][c]).
    float viewCM[16], projCM[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            viewCM[c*4+r] = view.m[c][r];
            projCM[c*4+r] = proj.m[c][r];
        }

    // Map our GizmoOp enum to ImGuizmo operation.
    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    if (gizmoOp_ == GizmoOp::Rotate) op = ImGuizmo::ROTATE;
    else if (gizmoOp_ == GizmoOp::Scale) op = ImGuizmo::SCALE;

    if (gizmoOp_ == GizmoOp::None) return;

    const bool snap = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
    float snapValues[3] = { snapTranslate_, snapTranslate_, snapTranslate_ };
    if (gizmoOp_ == GizmoOp::Scale) {
        snapValues[0] = snapScale_; snapValues[1] = snapScale_; snapValues[2] = snapScale_;
    }

    const bool wasUsing = ImGuizmo::IsUsing();
    ImGuizmo::Manipulate(viewCM, projCM, op, ImGuizmo::WORLD, matrix,
                         nullptr, snap ? snapValues : nullptr);

    if (ImGuizmo::IsUsing()) {
        dragging_ = true;

        // Decompose column-major result back to world-space TRS.
        float translation[3], rotation[3], scale[3];
        ImGuizmo::DecomposeMatrixToComponents(matrix, translation, rotation, scale);

        const core::math::Vec3 newWorldPos{ translation[0], translation[1], translation[2] };
        const core::math::Vec3 newWorldScl{ scale[0], scale[1], scale[2] };
        constexpr float kDeg2Rad = 3.14159265358979f / 180.0f;
        const core::math::Quat newWorldRot = core::math::fromEulerYxz(
            rotation[1] * kDeg2Rad,
            rotation[0] * kDeg2Rad,
            rotation[2] * kDeg2Rad);

        if (hasParent) {
            // Convert world TRS back to parent-relative local TRS.
            const core::Transform pw = core::ecs::computeWorldTransform(world, hc->parent);
            const core::math::Quat parentInvRot = core::math::conjugate(pw.rotation);
            const core::math::Vec3 diff = newWorldPos - pw.position;
            const core::math::Vec3 unscaled = core::math::rotate(parentInvRot, diff);
            tr->position = { unscaled.x / pw.scale.x,
                             unscaled.y / pw.scale.y,
                             unscaled.z / pw.scale.z };
            tr->rotation = parentInvRot * newWorldRot;
            tr->scale    = { newWorldScl.x / pw.scale.x,
                             newWorldScl.y / pw.scale.y,
                             newWorldScl.z / pw.scale.z };
        } else {
            tr->position = newWorldPos;
            tr->scale    = newWorldScl;
            tr->rotation = newWorldRot;
        }
    }

    // Clear drag flag on mouse release.
    if (wasUsing && !ImGuizmo::IsUsing()) {
        dragging_ = false;
        // TODO Phase 10: capture pre-drag transform for undo command
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

    if (overlays_.colliders) {
        // Collision geometry cache: loaded once per unique asset path.
        static std::unordered_map<std::string,
                                   std::optional<tools::CpuCollision>> collCache;
        world.forEachEntity([&](core::ecs::Entity e) {
            auto* col = world.tryGet<core::ColliderComponent>(e);
            if (!col || !col->enabled) return;
            auto* tr  = world.tryGet<core::Transform>(e);
            const Vec3 origin = tr ? tr->position : Vec3{0.f, 0.f, 0.f};
            const Vec3 off    = { col->offsetX + origin.x,
                                  col->offsetY + origin.y,
                                  col->offsetZ + origin.z };

            const ImU32 col32 = col->isTrigger
                ? IM_COL32(255, 220,  0, 190)   // yellow for triggers
                : IM_COL32(  0, 230,  0, 190);  // green for solid

            switch (col->shape) {
                case core::ColliderComponent::Shape::Box: {
                    const Vec3 h = { col->params.box.halfX,
                                     col->params.box.halfY,
                                     col->params.box.halfZ };
                    const Vec3 corners[8] = {
                        off + Vec3{-h.x,-h.y,-h.z}, off + Vec3{ h.x,-h.y,-h.z},
                        off + Vec3{ h.x, h.y,-h.z}, off + Vec3{-h.x, h.y,-h.z},
                        off + Vec3{-h.x,-h.y, h.z}, off + Vec3{ h.x,-h.y, h.z},
                        off + Vec3{ h.x, h.y, h.z}, off + Vec3{-h.x, h.y, h.z},
                    };
                    const int edges[12][2] = {
                        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
                    };
                    for (auto& ed : edges)
                        drawClippedLine(dl, corners[ed[0]], corners[ed[1]],
                                        viewProj, originX_, originY_,
                                        contentWidth_, contentHeight_, col32);
                    break;
                }
                case core::ColliderComponent::Shape::Sphere: {
                    const float r = col->params.sphere.radius;
                    constexpr int kSegs = 16;
                    constexpr float kTwoPi = 6.28318530f;
                    for (int seg = 0; seg < kSegs; ++seg) {
                        const float t0 = static_cast<float>(seg)     / kSegs * kTwoPi;
                        const float t1 = static_cast<float>(seg + 1) / kSegs * kTwoPi;
                        const float c0 = std::cos(t0), s0 = std::sin(t0);
                        const float c1 = std::cos(t1), s1 = std::sin(t1);
                        drawClippedLine(dl, off + Vec3{r*c0,r*s0,0}, off + Vec3{r*c1,r*s1,0},
                                        viewProj, originX_, originY_, contentWidth_, contentHeight_, col32);
                        drawClippedLine(dl, off + Vec3{0,r*c0,r*s0}, off + Vec3{0,r*c1,r*s1},
                                        viewProj, originX_, originY_, contentWidth_, contentHeight_, col32);
                        drawClippedLine(dl, off + Vec3{r*c0,0,r*s0}, off + Vec3{r*c1,0,r*s1},
                                        viewProj, originX_, originY_, contentWidth_, contentHeight_, col32);
                    }
                    break;
                }
                case core::ColliderComponent::Shape::Capsule: {
                    const float r  = col->params.capsule.radius;
                    const float hh = col->params.capsule.halfHeight;
                    const Vec3  top = { off.x, off.y + hh, off.z };
                    const Vec3  bot = { off.x, off.y - hh, off.z };
                    constexpr int kSegs = 12;
                    constexpr float kTwoPi = 6.28318530f;
                    for (int seg = 0; seg < kSegs; ++seg) {
                        const float t0 = static_cast<float>(seg)     / kSegs * kTwoPi;
                        const float t1 = static_cast<float>(seg + 1) / kSegs * kTwoPi;
                        const float c0 = std::cos(t0), s0 = std::sin(t0);
                        const float c1 = std::cos(t1), s1 = std::sin(t1);
                        drawClippedLine(dl, top + Vec3{r*c0,0,r*s0}, top + Vec3{r*c1,0,r*s1},
                                        viewProj, originX_, originY_, contentWidth_, contentHeight_, col32);
                        drawClippedLine(dl, bot + Vec3{r*c0,0,r*s0}, bot + Vec3{r*c1,0,r*s1},
                                        viewProj, originX_, originY_, contentWidth_, contentHeight_, col32);
                    }
                    for (int i = 0; i < 4; ++i) {
                        const float a = static_cast<float>(i) * 0.25f * kTwoPi;
                        const float ca = std::cos(a), sa = std::sin(a);
                        drawClippedLine(dl, top + Vec3{r*ca,0,r*sa}, bot + Vec3{r*ca,0,r*sa},
                                        viewProj, originX_, originY_, contentWidth_, contentHeight_, col32);
                    }
                    break;
                }
                case core::ColliderComponent::Shape::ConvexHull:
                case core::ColliderComponent::Shape::TriangleMesh: {
                    auto* mh = world.tryGet<core::MeshHandle>(e);
                    if (!mh || !mh->assetPath[0]) break;

                    // Per-path cache — loaded once, retained until the process exits.
                    const std::string key(mh->assetPath);
                    if (!collCache.count(key)) {
                        auto loaded = tools::loadEasset(std::filesystem::path(mh->assetPath));
                        collCache[key] = (loaded && loaded->collision.has_value())
                            ? std::move(loaded->collision) : std::nullopt;
                    }
                    const auto& maybeCol = collCache.at(key);
                    if (!maybeCol.has_value()) break;

                    const auto& cdata = *maybeCol;
                    const auto& verts = cdata.vertices;
                    if (verts.empty()) break;

                    if (col->shape == core::ColliderComponent::Shape::TriangleMesh) {
                        // Draw triangle soup wireframe; cap at 2000 edges total.
                        const auto& idx = cdata.indices;
                        const uint32_t triCount = static_cast<uint32_t>(idx.size() / 3);
                        constexpr uint32_t kMaxEdges = 2000;
                        uint32_t edgeCount = 0;
                        for (uint32_t t = 0; t < triCount && edgeCount < kMaxEdges; ++t) {
                            const auto& v0 = verts[idx[t*3+0]];
                            const auto& v1 = verts[idx[t*3+1]];
                            const auto& v2 = verts[idx[t*3+2]];
                            const Vec3 p0 = off + Vec3{v0[0], v0[1], v0[2]};
                            const Vec3 p1 = off + Vec3{v1[0], v1[1], v1[2]};
                            const Vec3 p2 = off + Vec3{v2[0], v2[1], v2[2]};
                            drawClippedLine(dl, p0, p1, viewProj, originX_, originY_,
                                            contentWidth_, contentHeight_, col32);
                            drawClippedLine(dl, p1, p2, viewProj, originX_, originY_,
                                            contentWidth_, contentHeight_, col32);
                            drawClippedLine(dl, p2, p0, viewProj, originX_, originY_,
                                            contentWidth_, contentHeight_, col32);
                            edgeCount += 3;
                        }
                        if (edgeCount >= kMaxEdges) {
                            ImVec2 labelPos;
                            if (worldToScreen(off, viewProj, originX_, originY_,
                                              contentWidth_, contentHeight_, labelPos)) {
                                dl->AddText(labelPos, col32, "[truncated]");
                            }
                        }
                    } else {
                        // ConvexHull — vertex soup, no index list.
                        // Draw all-pairs edges, capped at 200 to keep the overlay readable.
                        constexpr uint32_t kMaxHullEdges = 200;
                        uint32_t edgeCount = 0;
                        const uint32_t n = static_cast<uint32_t>(verts.size());
                        for (uint32_t i = 0; i < n && edgeCount < kMaxHullEdges; ++i) {
                            for (uint32_t j = i + 1; j < n && edgeCount < kMaxHullEdges; ++j) {
                                const Vec3 p0 = off + Vec3{verts[i][0], verts[i][1], verts[i][2]};
                                const Vec3 p1 = off + Vec3{verts[j][0], verts[j][1], verts[j][2]};
                                drawClippedLine(dl, p0, p1, viewProj, originX_, originY_,
                                                contentWidth_, contentHeight_, col32);
                                ++edgeCount;
                            }
                        }
                    }
                    break;
                }
            }
        });
    }

    // Camera entity icons — one icon per entity with Transform + rendering::Camera.
    if (overlays_.cameras) {
        core::ecs::View<core::Transform, rendering::Camera> camView(world);
        for (auto [entity, tr, cam] : camView) {
            ImVec2 screenPos;
            if (!worldToScreen(tr.position, viewProj,
                               originX_, originY_, contentWidth_, contentHeight_,
                               screenPos)) {
                continue;
            }
            // Clip to viewport bounds with a small margin so edge icons remain visible.
            if (screenPos.x < originX_ - 8.0f || screenPos.x > originX_ + contentWidth_  + 8.0f) continue;
            if (screenPos.y < originY_ - 8.0f || screenPos.y > originY_ + contentHeight_ + 8.0f) continue;

            const ImU32 col     = cam.isMain ? IM_COL32(255, 220,  60, 230)   // gold for main cam
                                             : IM_COL32(180, 180, 255, 200);  // pale blue for others
            const float radius  = 8.0f;
            dl->AddCircle(screenPos, radius, col, 0, 2.0f);
            // Small lens rectangle in front of the circle.
            dl->AddRect(ImVec2(screenPos.x - 5.0f, screenPos.y - 3.0f),
                        ImVec2(screenPos.x + 5.0f, screenPos.y + 3.0f),
                        col, 0.0f, 0, 1.5f);
            // Label.
            dl->AddText(ImVec2(screenPos.x + radius + 2.0f, screenPos.y - 6.0f),
                        col, cam.isMain ? "CAM [main]" : "CAM");
        }
    }

    // SpawnPoint entity icons.
    if (overlays_.spawnPoints) {
        core::ecs::View<core::Transform, core::SpawnPointComponent> spawnView(world);
        for (auto [entity, tr, sp] : spawnView) {
            ImVec2 screenPos;
            if (!worldToScreen(tr.position, viewProj,
                               originX_, originY_, contentWidth_, contentHeight_,
                               screenPos)) continue;
            if (screenPos.x < originX_ - 8.0f || screenPos.x > originX_ + contentWidth_  + 8.0f) continue;
            if (screenPos.y < originY_ - 8.0f || screenPos.y > originY_ + contentHeight_ + 8.0f) continue;

            const ImU32 col = sp.teamId == 0
                ? IM_COL32(80, 220, 80, 230)    // green = any team
                : IM_COL32(80, 180, 255, 230);  // blue = specific team
            dl->AddCircle(screenPos, 9.0f, col, 0, 2.0f);
            // Arrow pointing up to indicate "spawn here".
            dl->AddLine(screenPos, ImVec2(screenPos.x, screenPos.y - 14.0f), col, 2.0f);
            dl->AddLine(ImVec2(screenPos.x, screenPos.y - 14.0f),
                        ImVec2(screenPos.x - 4.0f, screenPos.y - 10.0f), col, 2.0f);
            dl->AddLine(ImVec2(screenPos.x, screenPos.y - 14.0f),
                        ImVec2(screenPos.x + 4.0f, screenPos.y - 10.0f), col, 2.0f);
            char lbl[32];
            std::snprintf(lbl, sizeof(lbl), sp.teamId ? "SPAWN[T%u]" : "SPAWN", sp.teamId);
            dl->AddText(ImVec2(screenPos.x + 11.0f, screenPos.y - 6.0f), col, lbl);
        }
    }

    // Trigger volume gizmos.
    if (overlays_.triggers) {
        core::ecs::View<core::Transform, core::TriggerComponent> trigView(world);
        const ImU32 col = IM_COL32(40, 220, 60, 200);
        for (auto [entity, tr, trig] : trigView) {
            ImVec2 screenPos;
            if (!worldToScreen(tr.position, viewProj,
                               originX_, originY_, contentWidth_, contentHeight_,
                               screenPos)) continue;
            if (screenPos.x < originX_ - 8.0f || screenPos.x > originX_ + contentWidth_  + 8.0f) continue;
            if (screenPos.y < originY_ - 8.0f || screenPos.y > originY_ + contentHeight_ + 8.0f) continue;

            if (trig.shape == core::ColliderComponent::Shape::Sphere) {
                const float r = trig.params.sphere.radius;
                // Project radius to screen: use a point offset by r on X axis.
                ImVec2 edgePt;
                const bool ok = worldToScreen(
                    {tr.position.x + r, tr.position.y, tr.position.z},
                    viewProj, originX_, originY_, contentWidth_, contentHeight_, edgePt);
                const float screenR = ok
                    ? std::abs(edgePt.x - screenPos.x)
                    : 12.0f;
                dl->AddCircle(screenPos, screenR, col, 0, 1.5f);
            } else {
                // Box: project the 4 visible corners (approximate 2D AABB).
                const auto& b = trig.params.box;
                float hw = b.halfX, hh = b.halfY;
                dl->AddRect(
                    ImVec2(screenPos.x - hw * 20.0f, screenPos.y - hh * 20.0f),
                    ImVec2(screenPos.x + hw * 20.0f, screenPos.y + hh * 20.0f),
                    col, 0.0f, 0, 1.5f);
            }
            dl->AddText(ImVec2(screenPos.x + 13.0f, screenPos.y - 6.0f), col, "TRIGGER");
        }
    }
}

void ViewportPanel::drawColliderHandles(core::ecs::World& world,
                                        core::ecs::Entity selected,
                                        const Mat4& viewProj,
                                        UndoStack& undo) {
    auto* col = world.tryGet<core::ColliderComponent>(selected);
    if (!col) return;
    auto* tr  = world.tryGet<core::Transform>(selected);
    const Vec3 center = tr ? tr->position : Vec3{0.f, 0.f, 0.f};
    const Vec3 off = { col->offsetX + center.x,
                       col->offsetY + center.y,
                       col->offsetZ + center.z };

    // Face-center positions for the 6 handles: +X,-X,+Y,-Y,+Z,-Z (indices 0-5).
    Vec3 facePos[6];
    switch (col->shape) {
        case core::ColliderComponent::Shape::Box:
            facePos[0] = off + Vec3{ col->params.box.halfX, 0.f, 0.f };
            facePos[1] = off + Vec3{-col->params.box.halfX, 0.f, 0.f };
            facePos[2] = off + Vec3{ 0.f,  col->params.box.halfY, 0.f };
            facePos[3] = off + Vec3{ 0.f, -col->params.box.halfY, 0.f };
            facePos[4] = off + Vec3{ 0.f, 0.f,  col->params.box.halfZ };
            facePos[5] = off + Vec3{ 0.f, 0.f, -col->params.box.halfZ };
            break;
        case core::ColliderComponent::Shape::Sphere: {
            const float r = col->params.sphere.radius;
            facePos[0] = off + Vec3{ r, 0.f, 0.f };
            facePos[1] = off + Vec3{-r, 0.f, 0.f };
            facePos[2] = off + Vec3{ 0.f,  r, 0.f };
            facePos[3] = off + Vec3{ 0.f, -r, 0.f };
            facePos[4] = off + Vec3{ 0.f, 0.f,  r };
            facePos[5] = off + Vec3{ 0.f, 0.f, -r };
            break;
        }
        case core::ColliderComponent::Shape::Capsule: {
            const float r  = col->params.capsule.radius;
            const float hh = col->params.capsule.halfHeight;
            facePos[0] = off + Vec3{ r, 0.f, 0.f };
            facePos[1] = off + Vec3{-r, 0.f, 0.f };
            facePos[2] = off + Vec3{ 0.f,  hh + r, 0.f };
            facePos[3] = off + Vec3{ 0.f, -(hh + r), 0.f };
            facePos[4] = off + Vec3{ 0.f, 0.f,  r };
            facePos[5] = off + Vec3{ 0.f, 0.f, -r };
            break;
        }
    }

    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImGuiIO&    io  = ImGui::GetIO();
    constexpr float kHandleRadius = 6.0f;
    constexpr float kHoverRadius  = 10.0f;

    // Axis colours: X=red, Y=green, Z=blue (±pair share the same axis colour).
    const ImU32 handleCols[6] = {
        IM_COL32(230, 70, 70, 255),  IM_COL32(230, 70, 70, 255),
        IM_COL32( 70,220, 70, 255),  IM_COL32( 70,220, 70, 255),
        IM_COL32( 80,120,240, 255),  IM_COL32( 80,120,240, 255),
    };

    ImVec2 screenPos[6];
    bool   visible[6] = {};
    for (int i = 0; i < 6; ++i) {
        visible[i] = worldToScreen(facePos[i], viewProj,
                                   originX_, originY_,
                                   contentWidth_, contentHeight_,
                                   screenPos[i]);
    }

    // Drag logic: start on LMB press over a handle, update until release.
    if (!colliderDragging_) {
        for (int i = 0; i < 6; ++i) {
            if (!visible[i]) continue;
            const float dx = io.MousePos.x - screenPos[i].x;
            const float dy = io.MousePos.y - screenPos[i].y;
            if (dx * dx + dy * dy <= kHoverRadius * kHoverRadius &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !dragging_) {
                colliderDragging_  = true;
                colliderHandleIdx_ = i;
                colliderDragStart_ = *col;
                break;
            }
        }
    }

    if (colliderDragging_) {
        // Map screen X delta to size delta along the axis of this handle.
        // Sign: handles 0,2,4 are + face; 1,3,5 are - face.
        const float delta = io.MouseDelta.x * 0.005f;
        const float sign  = (colliderHandleIdx_ % 2 == 0) ? 1.f : -1.f;
        const float grow  = sign * delta;

        switch (col->shape) {
            case core::ColliderComponent::Shape::Box:
                switch (colliderHandleIdx_ / 2) {
                    case 0: col->params.box.halfX = std::max(0.01f, col->params.box.halfX + grow); break;
                    case 1: col->params.box.halfY = std::max(0.01f, col->params.box.halfY + grow); break;
                    case 2: col->params.box.halfZ = std::max(0.01f, col->params.box.halfZ + grow); break;
                }
                break;
            case core::ColliderComponent::Shape::Sphere:
                col->params.sphere.radius = std::max(0.01f, col->params.sphere.radius + delta);
                break;
            case core::ColliderComponent::Shape::Capsule:
                if (colliderHandleIdx_ / 2 == 1) {
                    col->params.capsule.halfHeight = std::max(0.01f, col->params.capsule.halfHeight + grow);
                } else {
                    col->params.capsule.radius = std::max(0.01f, col->params.capsule.radius + delta);
                }
                break;
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            colliderDragging_ = false;
            undo.push(std::make_unique<ColliderResizeCommand>(
                world, selected, colliderDragStart_, *col));
        }
    }

    // Draw handles (after drag update so positions reflect live data).
    for (int i = 0; i < 6; ++i) {
        if (!visible[i]) continue;
        const bool active = colliderDragging_ && colliderHandleIdx_ == i;
        const ImU32 fillCol = active
            ? IM_COL32(255, 255, 255, 220)
            : handleCols[i];
        dl->AddCircleFilled(screenPos[i], kHandleRadius, fillCol);
        dl->AddCircle(screenPos[i], kHandleRadius, IM_COL32(0, 0, 0, 180), 0, 1.5f);
    }

    // TODO Phase 9: replace screen-space dot handles with full 3D ImGuizmo::DrawCubes interaction.
}

void ViewportPanel::handlePrefabDrop(const std::filesystem::path& path,
                                     core::ecs::World& world,
                                     core::ecs::Entity& selected) {
    auto prefabData = engine::tools::PrefabSerializer::load(path);
    if (!prefabData) return;

    core::ecs::SpawnParams params{};
    params.position = core::math::Vec3{0.f, 0.f, 0.f};

    core::ecs::Entity e = engine::tools::PrefabSerializer::instantiate(
        *prefabData, params, world);
    if (e == core::ecs::kInvalidEntity) return;

    core::ecs::PrefabInstance pi{};
    const std::string pathStr = path.string();
    strncpy_s(pi.sourcePrefabPath, sizeof(pi.sourcePrefabPath),
              pathStr.c_str(), _TRUNCATE);
    pi.overriddenComponents = 0u;
    world.addComponent<core::ecs::PrefabInstance>(e, pi);

    selected = e;
    if (sceneDirty_) *sceneDirty_ = true;

    // TODO Phase 9: push a SpawnPrefabCommand onto the undo stack.
}

void ViewportPanel::handleEassetDrop(const std::filesystem::path& path,
                                     core::ecs::World& world,
                                     core::ecs::Entity& selected,
                                     const EditorCamera& camera) {
    core::ecs::Entity e = world.createEntity();

    // Name it after the file stem.
    world.addComponent<core::ecs::Name>(e, core::ecs::Name(path.stem().string().c_str()));

    // Place it at the camera's current focus point so the user can orbit to
    // wherever they want geometry before dropping.
    core::Transform tr{};
    tr.position = camera.focus();
    world.addComponent<core::Transform>(e, tr);

    // Wire the asset path so MeshRenderSystem picks it up next frame.
    core::MeshHandle mh{};
    const std::string pathStr = path.string();
    strncpy_s(mh.assetPath, sizeof(mh.assetPath), pathStr.c_str(), _TRUNCATE);
    world.addComponent<core::MeshHandle>(e, mh);

    selected = e;
    if (sceneDirty_) *sceneDirty_ = true;
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
                         bool* open,
                         app::ViewMode& viewMode) {
    if (open && !*open) return;

    ImGuizmo::BeginFrame();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool visible = ImGui::Begin("Viewport", open);
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }

    focused_ = ImGui::IsWindowFocused();

    // Viewmode toolbar — Lit / Unlit dropdown.
    {
        const char* viewModeItems[] = { "Lit", "Unlit" };
        int vmi = static_cast<int>(viewMode);
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::Combo("##viewmode", &vmi, viewModeItems, 2))
            viewMode = static_cast<app::ViewMode>(vmi);
        ImGui::SameLine();
        ImGui::Separator();
    }

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

    // Accept .prefab and .easset drag-drop onto the viewport image.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            std::filesystem::path droppedPath(
                static_cast<const char*>(payload->Data));
            if (droppedPath.extension() == ".prefab") {
                handlePrefabDrop(droppedPath, world, selected);
            } else if (droppedPath.extension() == ".easset") {
                handleEassetDrop(droppedPath, world, selected, camera);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Build view-projection for overlays/gizmo/picking.
    const float aspect = contentWidth_ / contentHeight_;
    const Mat4 view = camera.viewMatrix();
    const Mat4 proj = core::math::perspectiveRhYupReverseZ(
        camera.fovYDegrees * (kPi / 180.0f), aspect, camera.nearZ, camera.farZ);
    const Mat4 viewProj = view * proj;

    handleCameraInput(camera, hovered);

    // Left-click in empty space (not dragging gizmo, not orbiting) picks.
    if (hovered && !dragging_ && !colliderDragging_ &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
        !ImGuizmo::IsOver() &&
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
        drawGizmo(world, selected, view, proj, undo);
        drawColliderHandles(world, selected, viewProj, undo);
    }
    drawOrientationWidget(view);

    ImGui::End();
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
