#pragma once
#ifdef ENGINE_DEVREL

#include "editor/EditorCamera.h"

#include <core/ecs/Entity.h>
#include <core/math/Mat.h>
#include <core/math/Vec.h>
#include <core/components/ColliderComponent.h>
#include <core/components/Transform.h>

#include <cstdint>
#include <filesystem>

namespace engine::core::ecs { class World; }

namespace engine::editor {

class UndoStack;
class SelectionSystem;

// Active gizmo operation, cycled with W/E/R/Q.
enum class GizmoOp : uint8_t { None, Translate, Rotate, Scale };

// Renders the scene texture, hosts the editor camera, draws manipulation gizmos
// (implemented with ImGui draw lists — no ImGuizmo dependency), and toggles
// debug overlays. The actual scene-to-texture render is done by EditorApp; this
// panel only consumes the resulting shader-resource handle.
class ViewportPanel {
public:
    struct Overlays {
        bool grid        = true;
        bool colliders   = false;
        bool boundingBox = true;
        bool spawnPoints = false;
        bool triggers    = false;
    };

    // sceneTextureSrv: GPU descriptor handle (.ptr) of the rendered scene SRV,
    // or 0 if no texture is available yet (panel then shows a placeholder).
    void draw(core::ecs::World& world,
              core::ecs::Entity& selected,
              EditorCamera& camera,
              SelectionSystem& picking,
              UndoStack& undo,
              uint64_t sceneTextureSrv,
              bool* open);

    // Viewport content region size in pixels, updated each draw().
    float contentWidth()  const noexcept { return contentWidth_; }
    float contentHeight() const noexcept { return contentHeight_; }
    bool  isFocused()     const noexcept { return focused_; }

    Overlays& overlays() noexcept { return overlays_; }
    GizmoOp   gizmoOp()  const noexcept { return gizmoOp_; }
    void      setGizmoOp(GizmoOp op) noexcept { gizmoOp_ = op; }
    bool      snapEnabled() const noexcept { return snapEnabled_; }

    // When true, WASD/look camera input is suppressed so the PIE player
    // entity's Transform drives the viewport view matrix instead.
    void setPieActive(bool active) noexcept { pieActive_ = active; }

    // Optional pointer to the editor dirty flag, set by EditorApp.
    void setSceneDirty(bool* flag) noexcept { sceneDirty_ = flag; }

private:
    void handleCameraInput(EditorCamera& camera, bool hovered);
    void drawGizmo(core::ecs::World& world,
                   core::ecs::Entity selected,
                   const core::math::Mat4& viewProj,
                   UndoStack& undo);
    void drawColliderHandles(core::ecs::World& world,
                             core::ecs::Entity selected,
                             const core::math::Mat4& viewProj,
                             UndoStack& undo);
    void drawOverlays(core::ecs::World& world,
                      const core::math::Mat4& viewProj);
    void drawOrientationWidget(const core::math::Mat4& view);
    void handlePrefabDrop(const std::filesystem::path& path,
                          core::ecs::World& world,
                          core::ecs::Entity& selected);

    Overlays overlays_;
    GizmoOp  gizmoOp_      = GizmoOp::Translate;
    bool     snapEnabled_  = false;
    float    snapTranslate_ = 0.5f;
    float    snapRotateDeg_ = 15.0f;
    float    snapScale_     = 0.25f;

    float contentWidth_  = 0.0f;
    float contentHeight_ = 0.0f;
    float originX_       = 0.0f;   // screen-space top-left of the image
    float originY_       = 0.0f;
    bool  focused_       = false;

    bool pieActive_   = false;
    bool* sceneDirty_ = nullptr;

    // Gizmo drag state (one drag => one undo command).
    bool            dragging_            = false;
    core::Transform dragStart_{};

    // Camera drag state — stays true while right mouse is held, even after the
    // cursor leaves the viewport item rect.
    bool            cameraRightDragging_ = false;

    // Collider handle drag state.
    bool                    colliderDragging_   = false;
    int                     colliderHandleIdx_  = -1;  // 0..5 = ±X,±Y,±Z
    core::ColliderComponent colliderDragStart_{};
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
