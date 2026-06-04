#pragma once

#include <physics/ColliderShape.h>
#include <physics/QueryFilter.h>
#include <core/math/AABB.h>
#include <core/math/Vec.h>
#include <core/math/Quat.h>
#include <core/ecs/Entity.h>
#include <vector>
#include <cstdint>

namespace engine::physics::internal {

// AABB of a shape in world space.
engine::core::math::AABB computeAABB(const Collider& collider,
                                     const engine::core::math::Vec3& position,
                                     const engine::core::math::Quat& rotation) noexcept;

struct BroadEntry {
    uint32_t              bodyIndex = 0;
    engine::core::math::AABB aabb;
};

// Binary BVH over static bodies (rebuilt on demand when statics change).
class StaticBVH {
public:
    void build(const std::vector<BroadEntry>& entries);
    void clear() noexcept;

    // Fill outIndices with bodyIndex values whose AABBs overlap queryAABB.
    void query(const engine::core::math::AABB& queryAABB,
               std::vector<uint32_t>& outIndices) const;

    // Returns the closest hit t (>= 0) or -1 on miss.  outIndex is set on hit.
    float raycast(const engine::core::math::Vec3& origin,
                  const engine::core::math::Vec3& dir,
                  float maxDist,
                  uint32_t& outIndex) const;

private:
    struct Node {
        engine::core::math::AABB aabb;
        int  left      = -1;  // -1 means leaf
        int  right     = -1;
        int  entryIdx  = -1;  // valid for leaves
    };

    void buildRecursive(int nodeIdx,
                        std::vector<int>& entryIndices,
                        const std::vector<BroadEntry>& entries,
                        int first, int count);

    void queryRecursive(int nodeIdx,
                        const engine::core::math::AABB& queryAABB,
                        std::vector<uint32_t>& outIndices) const;

    float raycastRecursive(int nodeIdx,
                           const engine::core::math::Vec3& origin,
                           const engine::core::math::Vec3& dir,
                           float maxDist,
                           uint32_t& outIndex) const;

    std::vector<Node>      nodes_;
    std::vector<BroadEntry> entries_;
};

// Uniform spatial hash grid for dynamic bodies (4 m cell size).
class DynamicGrid {
public:
    static constexpr float kCellSize = 4.0f;

    void clear() noexcept;
    void insert(uint32_t bodyIndex, const engine::core::math::AABB& aabb);

    void query(const engine::core::math::AABB& queryAABB,
               std::vector<uint32_t>& outIndices) const;

private:
    struct Cell {
        std::vector<uint32_t> indices;
    };

    // Simple open-addressing hash of (ix, iy, iz) -> Cell.
    static uint64_t cellKey(int ix, int iy, int iz) noexcept;

    // unordered_map would require hashing; use a flat sorted vector for simplicity.
    std::vector<std::pair<uint64_t, Cell>> cells_;
    Cell* findOrCreate(uint64_t key);
    const Cell* find(uint64_t key) const;
};

} // namespace engine::physics::internal
