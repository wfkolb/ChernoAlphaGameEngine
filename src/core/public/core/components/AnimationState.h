#pragma once
#include <core/ecs/Entity.h>
#include <cstdint>
#include <type_traits>

namespace engine::core {

struct AnimationState {
    static constexpr ecs::ComponentTypeId kComponentId = 10;

    enum class Clip : uint8_t {
        Idle = 0,
        Walk,
        Run,
        Jump,
        Fall,
        Land,
        Count
    };

    Clip    currentClip     = Clip::Idle;
    Clip    previousClip    = Clip::Idle;
    uint8_t _pad0           = 0;
    uint8_t _pad1           = 0;
    float   clipTimeSeconds = 0.f;
    float   blendWeight     = 1.f;
    bool    isGrounded      = false;
    uint8_t _pad2           = 0;
    uint8_t _pad3           = 0;
    uint8_t _pad4           = 0;
};

static_assert(std::is_trivially_copyable_v<AnimationState>,
              "AnimationState must be trivially copyable for ECS archetype moves");

} // namespace engine::core
