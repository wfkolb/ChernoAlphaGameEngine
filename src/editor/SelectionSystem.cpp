#ifdef ENGINE_DEVREL

#include "editor/SelectionSystem.h"

#include <core/ecs/World.h>
#include <core/components/Transform.h>

#include <algorithm>
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

// ---- Multi-select -----------------------------------------------------------

void SelectionSystem::selectOnly(core::ecs::Entity e) {
    selection_.clear();
    if (e != core::ecs::kInvalidEntity) {
        selection_.push_back(e);
    }
    pivot_ = e;
}

void SelectionSystem::toggleSelect(core::ecs::Entity e) {
    if (e == core::ecs::kInvalidEntity) return;
    auto it = std::find(selection_.begin(), selection_.end(), e);
    if (it != selection_.end()) {
        selection_.erase(it);
        if (pivot_ == e) {
            pivot_ = selection_.empty() ? core::ecs::kInvalidEntity : selection_.back();
        }
    } else {
        selection_.push_back(e);
        pivot_ = e;
    }
}

void SelectionSystem::rangeSelect(core::ecs::Entity e,
                                  const std::vector<core::ecs::Entity>& orderedEntities) {
    if (e == core::ecs::kInvalidEntity) return;

    // Locate pivot and target in the ordered list.
    const core::ecs::Entity effectivePivot =
        (pivot_ != core::ecs::kInvalidEntity) ? pivot_ : e;

    auto pivotIt  = std::find(orderedEntities.begin(), orderedEntities.end(), effectivePivot);
    auto targetIt = std::find(orderedEntities.begin(), orderedEntities.end(), e);

    if (pivotIt == orderedEntities.end() || targetIt == orderedEntities.end()) {
        selectOnly(e);
        return;
    }

    if (pivotIt > targetIt) std::swap(pivotIt, targetIt);

    for (auto it = pivotIt; it <= targetIt; ++it) {
        if (std::find(selection_.begin(), selection_.end(), *it) == selection_.end()) {
            selection_.push_back(*it);
        }
    }
    // Do not move pivot on a range extension.
}

bool SelectionSystem::isSelected(core::ecs::Entity e) const noexcept {
    return std::find(selection_.begin(), selection_.end(), e) != selection_.end();
}

core::math::Vec3 SelectionSystem::selectionCentroid(core::ecs::World& world) const {
    Vec3 sum = Vec3::zero();
    int  count = 0;
    for (core::ecs::Entity e : selection_) {
        if (const auto* tr = world.tryGet<core::Transform>(e)) {
            sum += tr->position;
            ++count;
        }
    }
    return (count > 0) ? sum * (1.0f / static_cast<float>(count)) : Vec3::zero();
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
