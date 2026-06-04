#pragma once
#ifdef ENGINE_DEVREL

#include <core/ecs/Entity.h>
#include <core/math/Vec.h>
#include <core/math/Mat.h>
#include <core/math/AABB.h>

#include <vector>

namespace engine::core::ecs { class World; }

namespace engine::editor {

// Screen-space entity picking.
//
// The spec calls for a GPU object-ID buffer; the current renderer does not
// expose an object-ID pass, so picking is done on the CPU by unprojecting the
// cursor into a world ray and intersecting it against per-entity bounds. The
// interface is GPU-agnostic, so a GPU id-buffer can be slotted behind
// pickAtPixel() later without touching callers.
class SelectionSystem {
public:
    struct Pickable {
        core::ecs::Entity entity;
        core::math::AABB  worldBounds;
    };

    // Rebuild the pickable set for this frame. The editor fills `out` from the
    // active world (entities with a Transform get a default unit AABB unless a
    // tighter bound is known).
    void setPickables(std::vector<Pickable> pickables) { pickables_ = std::move(pickables); }

    // Pick the nearest entity under a pixel. viewProj maps world -> clip (row
    // vector convention: clip = world * viewProj). px/py are in pixels with the
    // origin at the top-left of the viewport region of size (w,h).
    // Returns kInvalidEntity on a miss.
    core::ecs::Entity pickAtPixel(float px, float py,
                                  float viewportW, float viewportH,
                                  const core::math::Mat4& viewProj) const;

    // Build a world-space ray (origin + normalized direction) from a pixel.
    static void rayFromPixel(float px, float py,
                             float viewportW, float viewportH,
                             const core::math::Mat4& invViewProj,
                             core::math::Vec3& outOrigin,
                             core::math::Vec3& outDir);

    const std::vector<Pickable>& pickables() const noexcept { return pickables_; }

private:
    std::vector<Pickable> pickables_;
};

// Ray/AABB slab test. Returns true and sets tHit (>= 0) on intersection.
bool rayIntersectsAABB(const core::math::Vec3& origin,
                       const core::math::Vec3& dir,
                       const core::math::AABB& box,
                       float& tHit);

} // namespace engine::editor

#endif // ENGINE_DEVREL
