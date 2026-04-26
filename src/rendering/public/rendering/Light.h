#pragma once

#include <cstdint>

namespace engine::rendering {

    // ECS component — describes a light source.
    // Direction and position are derived from the entity's Transform component.
    // color: linear RGB; intensity: lux (directional), candela (point/spot).
    // range: effective radius in metres (point/spot only).
    // innerConeAngle / outerConeAngle: spot lights only, in radians.
    struct Light {
        enum class Type : uint8_t {
            Directional = 0,
            Point       = 1,
            Spot        = 2,
        };

        Type  type           { Type::Directional };
        float color[3]       { 1.0f, 1.0f, 1.0f };
        float intensity      { 1.0f };
        float range          { 10.0f };
        float innerConeAngle { 0.0f };
        float outerConeAngle { 0.785f };  // ~45 degrees
        bool  castShadow     { false };
    };

} // namespace engine::rendering
