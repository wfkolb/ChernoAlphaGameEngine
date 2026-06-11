#pragma once
#include <core/ecs/Entity.h>
#include <type_traits>
#include <cstdint>

namespace engine::core {

// Trivially-copyable ECS component describing a collision shape.
// Box, Sphere, Capsule carry inline parameters for editor authoring.
// ConvexHull and TriangleMesh are mesh-derived shapes: the actual geometry
// lives in the .easset file referenced by MeshHandle on the same entity.
// The physics module reads the MeshHandle asset path to obtain vertex data.
struct ColliderComponent {
    static constexpr ecs::ComponentTypeId kComponentId = 9;

    enum class Shape : uint8_t {
        Box         = 0,
        Sphere      = 1,
        Capsule     = 2,
        ConvexHull  = 3, // mesh-derived; geometry loaded from MeshHandle asset
        TriangleMesh = 4, // mesh-derived; geometry loaded from MeshHandle asset
    };

    struct BoxParams     { float halfX = 0.5f; float halfY = 0.5f; float halfZ = 0.5f; };
    struct SphereParams  { float radius = 0.5f; float _p0 = 0.f; float _p1 = 0.f; };
    struct CapsuleParams { float radius = 0.3f; float halfHeight = 0.5f; float _p0 = 0.f; };

    // All shapes share the same 12-byte footprint so the union is trivially copyable.
    union Params {
        BoxParams     box;
        SphereParams  sphere;
        CapsuleParams capsule;
    };

    Shape   shape         = Shape::Box;
    bool    enabled       = true;
    uint8_t _pad1         = 0;
    uint8_t _pad2         = 0;
    Params  params        = { .box = {} };
    float   offsetX       = 0.f;
    float   offsetY       = 0.f;
    float   offsetZ       = 0.f;
    uint8_t layerIndex    = 0;
    uint8_t materialIndex = 0;
    bool    isTrigger     = false;
    uint8_t _pad3         = 0;
};

static_assert(std::is_trivially_copyable_v<ColliderComponent>,
              "ColliderComponent must be trivially copyable for ECS archetype moves");

} // namespace engine::core
