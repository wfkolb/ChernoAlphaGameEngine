#ifdef ENGINE_DEVREL

#include "editor/SelectionSystem.h"

#include <limits>

namespace engine::editor {

using core::math::Vec3;
using core::math::Vec4;
using core::math::Mat4;

bool rayIntersectsAABB(const Vec3& origin, const Vec3& dir,
                       const core::math::AABB& box, float& tHit) {
    const float t = box.raycast(origin, dir);
    if (t < 0.0f) return false;
    tHit = t;
    return true;
}

void SelectionSystem::rayFromPixel(float px, float py,
                                   float viewportW, float viewportH,
                                   const Mat4& invViewProj,
                                   Vec3& outOrigin, Vec3& outDir) {
    // Pixel -> NDC. NDC x is [-1,1] left->right; y is [-1,1] bottom->top, so the
    // pixel y (top-down) is flipped.
    const float ndcX = (2.0f * px / viewportW) - 1.0f;
    const float ndcY = 1.0f - (2.0f * py / viewportH);

    // Reverse-Z: near plane is at depth 1, far plane at depth 0.
    const Vec4 nearClip{ ndcX, ndcY, 1.0f, 1.0f };
    const Vec4 farClip { ndcX, ndcY, 0.0f, 1.0f };

    Vec4 nearW = nearClip * invViewProj;
    Vec4 farW  = farClip  * invViewProj;
    if (nearW.w != 0.0f) nearW = nearW / nearW.w;
    if (farW.w  != 0.0f) farW  = farW  / farW.w;

    outOrigin = nearW.xyz();
    outDir    = core::math::normalize(farW.xyz() - nearW.xyz());
}

core::ecs::Entity SelectionSystem::pickAtPixel(float px, float py,
                                               float viewportW, float viewportH,
                                               const Mat4& viewProj) const {
    bool ok = false;
    const Mat4 invViewProj = core::math::inverse(viewProj, &ok);
    if (!ok) return core::ecs::kInvalidEntity;

    Vec3 origin, dir;
    rayFromPixel(px, py, viewportW, viewportH, invViewProj, origin, dir);

    core::ecs::Entity best = core::ecs::kInvalidEntity;
    float bestT = std::numeric_limits<float>::max();

    for (const Pickable& p : pickables_) {
        float t = 0.0f;
        if (rayIntersectsAABB(origin, dir, p.worldBounds, t) && t < bestT) {
            bestT = t;
            best  = p.entity;
        }
    }
    return best;
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
