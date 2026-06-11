#pragma once

#include <core/math/Vec.h>
#include <variant>
#include <vector>
#include <cstdint>

namespace engine::physics {

struct BoxShape {
    engine::core::math::Vec3 halfExtents = {0.5f, 0.5f, 0.5f};
};

struct SphereShape {
    float radius = 0.5f;
};

// radius = hemisphere radius; halfHeight = half the cylindrical portion (not total height)
struct CapsuleShape {
    float radius     = 0.3f;
    float halfHeight = 0.5f;
};

struct ConvexHullShape {
    std::vector<engine::core::math::Vec3> vertices;
};

struct TriangleMeshShape {
    std::vector<engine::core::math::Vec3> vertices;
    std::vector<uint32_t>                 indices;
};

using ColliderShapeVariant = std::variant<
    BoxShape,
    SphereShape,
    CapsuleShape,
    ConvexHullShape,
    TriangleMeshShape
>;

struct Collider {
    ColliderShapeVariant shape;
    engine::core::math::Vec3 localOffset = engine::core::math::Vec3::zero();
    uint8_t layerIndex    = 0;
    uint8_t materialIndex = 0;
    bool    isTrigger     = false;
    bool    enabled       = true;
};

} // namespace engine::physics
