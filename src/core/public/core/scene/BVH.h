#pragma once

#include <core/ecs/Entity.h>
#include <core/math/AABB.h>
#include <core/math/Frustum.h>
#include <core/math/Constants.h>
#include <vector>

namespace engine::core::scene {

struct BVHEntry {
    math::AABB      aabb;
    ecs::Entity     entity;
};

struct BVHRayHit {
    ecs::Entity  entity   = ecs::kInvalidEntity;
    float        distance = 0.0f;
    bool         hasHit   = false;
};

// Axis-aligned BVH over static scene objects (built once at scene activation).
// Supports ray and frustum queries for rendering/AI visibility.
class BVH {
public:
    BVH()  = default;
    ~BVH() = default;

    BVH(const BVH&)            = default;
    BVH& operator=(const BVH&) = default;
    BVH(BVH&&)                 = default;
    BVH& operator=(BVH&&)      = default;

    // Build from a set of entries. Old data is discarded.
    void build(const std::vector<BVHEntry>& entries);
    void clear() noexcept;

    bool empty() const noexcept { return nodes_.empty(); }
    int  entryCount() const noexcept { return static_cast<int>(entries_.size()); }

    // Returns the closest ray hit (or kInvalidEntity / hasHit=false on miss).
    BVHRayHit query(const math::Vec3& origin,
                    const math::Vec3& dir,
                    float maxDist = math::kInfinity) const noexcept;

    // Appends entities whose AABBs intersect the frustum to outEntities.
    void queryFrustum(const math::Frustum& frustum,
                      std::vector<ecs::Entity>& outEntities) const;

private:
    struct Node {
        math::AABB aabb;
        int        left     = -1;  // -1 == leaf
        int        right    = -1;
        int        entryIdx = -1;  // valid for leaves
    };

    std::vector<Node>     nodes_;
    std::vector<BVHEntry> entries_;

    void buildRecursive(int nodeIdx,
                        std::vector<int>& indices,
                        int first, int count);

    BVHRayHit queryRayRecursive(int nodeIdx,
                                const math::Vec3& origin,
                                const math::Vec3& dir,
                                float maxDist) const noexcept;

    void queryFrustumRecursive(int nodeIdx,
                               const math::Frustum& frustum,
                               std::vector<ecs::Entity>& out) const;
};

} // namespace engine::core::scene
