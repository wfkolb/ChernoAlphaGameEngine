#include "core/scene/BVH.h"
#include <core/math/Constants.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::core::scene {

using namespace engine::core::math;

// ── BVH build ─────────────────────────────────────────────────────────────────

void BVH::clear() noexcept {
    nodes_.clear();
    entries_.clear();
}

void BVH::build(const std::vector<BVHEntry>& entries) {
    clear();
    if (entries.empty()) return;

    entries_ = entries;
    const int n = static_cast<int>(entries_.size());

    // Allocate worst-case node count (2n-1 for a full binary tree).
    nodes_.resize(static_cast<size_t>(2 * n));
    int nodeCount = 1;

    // Compute root AABB.
    nodes_[0].aabb = AABB{};
    for (const auto& e : entries_) nodes_[0].aabb.expand(e.aabb);

    struct Task { int nodeIdx; int first; int count; };
    std::vector<Task> stack;
    std::vector<int>  indices(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) indices[i] = i;

    stack.push_back({0, 0, n});

    while (!stack.empty()) {
        auto [ni, first, count] = stack.back();
        stack.pop_back();

        if (count == 1) {
            nodes_[ni].left     = -1;
            nodes_[ni].right    = -1;
            nodes_[ni].entryIdx = indices[first];
            continue;
        }

        // Determine longest AABB axis among entry centroids for split.
        AABB centroidAABB;
        for (int i = first; i < first + count; ++i)
            centroidAABB.expand(entries_[indices[i]].aabb.center());

        const Vec3 sz = centroidAABB.size();
        const int  axis = (sz.x >= sz.y && sz.x >= sz.z) ? 0
                        : (sz.y >= sz.z) ? 1 : 2;

        const Vec3  centroidMid = centroidAABB.center();
        const float mid = axis == 0 ? centroidMid.x : (axis == 1 ? centroidMid.y : centroidMid.z);

        auto pivot = std::partition(
            indices.begin() + first,
            indices.begin() + first + count,
            [&](int idx) {
                const Vec3 c = entries_[idx].aabb.center();
                const float cv = axis == 0 ? c.x : (axis == 1 ? c.y : c.z);
                return cv < mid;
            });

        int split = static_cast<int>(std::distance(indices.begin(), pivot)) - first;
        if (split <= 0 || split >= count) split = count / 2;

        const int leftIdx  = nodeCount++;
        const int rightIdx = nodeCount++;
        nodes_[ni].left  = leftIdx;
        nodes_[ni].right = rightIdx;

        nodes_[leftIdx].aabb  = AABB{};
        nodes_[rightIdx].aabb = AABB{};
        for (int i = first;         i < first + split; ++i)
            nodes_[leftIdx].aabb.expand(entries_[indices[i]].aabb);
        for (int i = first + split; i < first + count; ++i)
            nodes_[rightIdx].aabb.expand(entries_[indices[i]].aabb);

        stack.push_back({leftIdx,  first,         split});
        stack.push_back({rightIdx, first + split, count - split});
    }

    nodes_.resize(static_cast<size_t>(nodeCount));
}

// ── Ray query ─────────────────────────────────────────────────────────────────

BVHRayHit BVH::query(const Vec3& origin, const Vec3& dir, float maxDist) const noexcept {
    if (nodes_.empty()) return {};
    return queryRayRecursive(0, origin, dir, maxDist);
}

BVHRayHit BVH::queryRayRecursive(int ni, const Vec3& origin,
                                  const Vec3& dir, float maxDist) const noexcept {
    if (ni < 0 || ni >= static_cast<int>(nodes_.size())) return {};
    const Node& node = nodes_[ni];

    const float aabbT = node.aabb.raycast(origin, dir);
    if (aabbT < 0.0f || aabbT > maxDist) return {};

    // Leaf — return this entry as a hit at the AABB entry point.
    if (node.left == -1) {
        BVHRayHit hit;
        hit.hasHit   = true;
        hit.distance = aabbT;
        hit.entity   = entries_[node.entryIdx].entity;
        return hit;
    }

    const BVHRayHit lh = queryRayRecursive(node.left,  origin, dir, maxDist);
    const BVHRayHit rh = queryRayRecursive(node.right, origin, dir, maxDist);

    if (!lh.hasHit && !rh.hasHit) return {};
    if (!lh.hasHit) return rh;
    if (!rh.hasHit) return lh;
    return lh.distance <= rh.distance ? lh : rh;
}

// ── Frustum query ─────────────────────────────────────────────────────────────

void BVH::queryFrustum(const Frustum& frustum,
                        std::vector<ecs::Entity>& outEntities) const {
    if (nodes_.empty()) return;
    queryFrustumRecursive(0, frustum, outEntities);
}

void BVH::queryFrustumRecursive(int ni, const Frustum& frustum,
                                 std::vector<ecs::Entity>& out) const {
    if (ni < 0 || ni >= static_cast<int>(nodes_.size())) return;
    const Node& node = nodes_[ni];

    if (!frustumContainsAabb(frustum, node.aabb.min, node.aabb.max)) return;

    if (node.left == -1) {
        out.push_back(entries_[node.entryIdx].entity);
        return;
    }

    queryFrustumRecursive(node.left,  frustum, out);
    queryFrustumRecursive(node.right, frustum, out);
}

} // namespace engine::core::scene
