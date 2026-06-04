#include "BroadPhase.h"
#include "NarrowPhase.h"
#include <core/math/Constants.h>
#include <algorithm>
#include <cmath>
#include <cassert>

namespace engine::physics::internal {

using namespace engine::core::math;

// ── AABB computation ─────────────────────────────────────────────────────────

AABB computeAABB(const Collider& collider,
                 const Vec3& position, const Quat& rotation) noexcept {
    const Vec3 worldPos = position + rotateByQuat(rotation, collider.localOffset);

    return std::visit([&](const auto& shape) -> AABB {
        using T = std::decay_t<decltype(shape)>;

        if constexpr (std::is_same_v<T, SphereShape>) {
            return AABB::fromCenterExtents(worldPos, {shape.radius, shape.radius, shape.radius});
        }
        else if constexpr (std::is_same_v<T, BoxShape>) {
            // OBB → AABB: project each local axis
            const Vec3 rx = rotateByQuat(rotation, {shape.halfExtents.x, 0, 0});
            const Vec3 ry = rotateByQuat(rotation, {0, shape.halfExtents.y, 0});
            const Vec3 rz = rotateByQuat(rotation, {0, 0, shape.halfExtents.z});
            const Vec3 he = {
                std::abs(rx.x) + std::abs(ry.x) + std::abs(rz.x),
                std::abs(rx.y) + std::abs(ry.y) + std::abs(rz.y),
                std::abs(rx.z) + std::abs(ry.z) + std::abs(rz.z),
            };
            return AABB::fromCenterExtents(worldPos, he);
        }
        else if constexpr (std::is_same_v<T, CapsuleShape>) {
            const Vec3 up = rotateByQuat(rotation, Vec3::unitY());
            const float r = shape.radius;
            const Vec3 he = {
                std::abs(up.x) * shape.halfHeight + r,
                std::abs(up.y) * shape.halfHeight + r,
                std::abs(up.z) * shape.halfHeight + r,
            };
            return AABB::fromCenterExtents(worldPos, he);
        }
        else if constexpr (std::is_same_v<T, ConvexHullShape>) {
            AABB aabb;
            for (const Vec3& v : shape.vertices) {
                aabb.expand(worldPos + rotateByQuat(rotation, v));
            }
            aabb.expandBy(0.01f);
            return aabb;
        }
        else { // TriangleMeshShape
            AABB aabb;
            for (const Vec3& v : shape.vertices) {
                aabb.expand(worldPos + rotateByQuat(rotation, v));
            }
            aabb.expandBy(0.01f);
            return aabb;
        }
    }, collider.shape);
}

// ── StaticBVH ────────────────────────────────────────────────────────────────

void StaticBVH::clear() noexcept {
    nodes_.clear();
    entries_.clear();
}

void StaticBVH::build(const std::vector<BroadEntry>& entries) {
    clear();
    if (entries.empty()) return;
    entries_ = entries;

    std::vector<int> indices(entries.size());
    for (int i = 0; i < static_cast<int>(indices.size()); ++i) indices[i] = i;

    nodes_.resize(2 * static_cast<int>(entries.size()) - 1);
    int nodeCount = 0;
    nodes_[0] = Node{};
    nodeCount = 1;

    // Compute root AABB
    for (const auto& e : entries) nodes_[0].aabb.expand(e.aabb);

    // Recursive build using index slices
    struct BuildTask { int nodeIdx; int first; int count; };
    std::vector<BuildTask> stack;
    stack.push_back({0, 0, static_cast<int>(entries.size())});

    while (!stack.empty()) {
        auto [ni, first, count] = stack.back();
        stack.pop_back();

        if (count == 1) {
            nodes_[ni].left  = -1;
            nodes_[ni].right = -1;
            nodes_[ni].entryIdx = indices[first];
            continue;
        }

        // Split on the longest axis at the centroid midpoint
        Vec3 centroidSum = Vec3::zero();
        for (int i = first; i < first + count; ++i) {
            centroidSum += entries_[indices[i]].aabb.center();
        }
        const Vec3 centroidMid = centroidSum * (1.0f / static_cast<float>(count));

        // Determine longest axis
        AABB centroidAABB;
        for (int i = first; i < first + count; ++i) {
            centroidAABB.expand(entries_[indices[i]].aabb.center());
        }
        const Vec3 sz = centroidAABB.size();
        const int axis = (sz.x >= sz.y && sz.x >= sz.z) ? 0
                        : (sz.y >= sz.z) ? 1 : 2;

        const float* mid = &centroidMid.x;
        const auto pivot = std::partition(
            indices.begin() + first,
            indices.begin() + first + count,
            [&](int idx) {
                const Vec3 c = entries_[idx].aabb.center();
                return (&c.x)[axis] < mid[axis];
            }
        );
        int split = static_cast<int>(std::distance(indices.begin(), pivot)) - first;
        if (split <= 0 || split >= count) split = count / 2;

        // Allocate children
        const int leftIdx  = nodeCount++;
        const int rightIdx = nodeCount++;
        nodes_[ni].left  = leftIdx;
        nodes_[ni].right = rightIdx;

        // Build child AABBs
        nodes_[leftIdx].aabb  = AABB{};
        nodes_[rightIdx].aabb = AABB{};
        for (int i = first; i < first + split; ++i)
            nodes_[leftIdx].aabb.expand(entries_[indices[i]].aabb);
        for (int i = first + split; i < first + count; ++i)
            nodes_[rightIdx].aabb.expand(entries_[indices[i]].aabb);

        stack.push_back({leftIdx,  first,         split});
        stack.push_back({rightIdx, first + split, count - split});
    }
}

void StaticBVH::query(const AABB& queryAABB, std::vector<uint32_t>& outIndices) const {
    if (nodes_.empty()) return;
    queryRecursive(0, queryAABB, outIndices);
}

void StaticBVH::queryRecursive(int nodeIdx, const AABB& queryAABB,
                               std::vector<uint32_t>& outIndices) const {
    if (nodeIdx < 0 || nodeIdx >= static_cast<int>(nodes_.size())) return;
    const Node& node = nodes_[nodeIdx];
    if (!node.aabb.intersects(queryAABB)) return;
    if (node.left == -1) {
        outIndices.push_back(entries_[node.entryIdx].bodyIndex);
        return;
    }
    queryRecursive(node.left,  queryAABB, outIndices);
    queryRecursive(node.right, queryAABB, outIndices);
}

float StaticBVH::raycast(const Vec3& origin, const Vec3& dir,
                         float maxDist, uint32_t& outIndex) const {
    if (nodes_.empty()) return -1.0f;
    outIndex = ~0u;
    return raycastRecursive(0, origin, dir, maxDist, outIndex);
}

float StaticBVH::raycastRecursive(int nodeIdx, const Vec3& origin, const Vec3& dir,
                                  float maxDist, uint32_t& outIndex) const {
    if (nodeIdx < 0 || nodeIdx >= static_cast<int>(nodes_.size())) return -1.0f;
    const Node& node = nodes_[nodeIdx];
    const float aabbT = node.aabb.raycast(origin, dir);
    if (aabbT < 0.0f || aabbT > maxDist) return -1.0f;

    if (node.left == -1) {
        outIndex = entries_[node.entryIdx].bodyIndex;
        return aabbT;
    }

    uint32_t li = ~0u, ri = ~0u;
    const float lt = raycastRecursive(node.left,  origin, dir, maxDist, li);
    const float rt = raycastRecursive(node.right, origin, dir, maxDist, ri);

    if (lt >= 0.0f && (rt < 0.0f || lt <= rt)) { outIndex = li; return lt; }
    if (rt >= 0.0f) { outIndex = ri; return rt; }
    return -1.0f;
}

// ── DynamicGrid ──────────────────────────────────────────────────────────────

uint64_t DynamicGrid::cellKey(int ix, int iy, int iz) noexcept {
    // Pack three 20-bit integers with 2-bit overlap protection
    constexpr uint64_t M = (1u << 20) - 1u;
    return (static_cast<uint64_t>(ix & M))
         | (static_cast<uint64_t>(iy & M) << 20)
         | (static_cast<uint64_t>(iz & M) << 40);
}

DynamicGrid::Cell* DynamicGrid::findOrCreate(uint64_t key) {
    for (auto& [k, cell] : cells_) {
        if (k == key) return &cell;
    }
    cells_.push_back({key, {}});
    return &cells_.back().second;
}

const DynamicGrid::Cell* DynamicGrid::find(uint64_t key) const {
    for (const auto& [k, cell] : cells_) {
        if (k == key) return &cell;
    }
    return nullptr;
}

void DynamicGrid::clear() noexcept {
    cells_.clear();
}

void DynamicGrid::insert(uint32_t bodyIndex, const AABB& aabb) {
    const int x0 = static_cast<int>(std::floor(aabb.min.x / kCellSize));
    const int y0 = static_cast<int>(std::floor(aabb.min.y / kCellSize));
    const int z0 = static_cast<int>(std::floor(aabb.min.z / kCellSize));
    const int x1 = static_cast<int>(std::floor(aabb.max.x / kCellSize));
    const int y1 = static_cast<int>(std::floor(aabb.max.y / kCellSize));
    const int z1 = static_cast<int>(std::floor(aabb.max.z / kCellSize));

    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                findOrCreate(cellKey(x, y, z))->indices.push_back(bodyIndex);
}

void DynamicGrid::query(const AABB& queryAABB, std::vector<uint32_t>& outIndices) const {
    const int x0 = static_cast<int>(std::floor(queryAABB.min.x / kCellSize));
    const int y0 = static_cast<int>(std::floor(queryAABB.min.y / kCellSize));
    const int z0 = static_cast<int>(std::floor(queryAABB.min.z / kCellSize));
    const int x1 = static_cast<int>(std::floor(queryAABB.max.x / kCellSize));
    const int y1 = static_cast<int>(std::floor(queryAABB.max.y / kCellSize));
    const int z1 = static_cast<int>(std::floor(queryAABB.max.z / kCellSize));

    for (int x = x0; x <= x1; ++x) {
        for (int y = y0; y <= y1; ++y) {
            for (int z = z0; z <= z1; ++z) {
                const Cell* cell = find(cellKey(x, y, z));
                if (!cell) continue;
                for (uint32_t idx : cell->indices) {
                    // Avoid duplicates (body can span multiple cells)
                    bool found = false;
                    for (uint32_t existing : outIndices) {
                        if (existing == idx) { found = true; break; }
                    }
                    if (!found) outIndices.push_back(idx);
                }
            }
        }
    }
}

} // namespace engine::physics::internal
