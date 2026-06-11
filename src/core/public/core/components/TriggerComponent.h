#pragma once
#include <core/ecs/Entity.h>
#include <core/components/ColliderComponent.h>
#include <type_traits>
#include <cstdint>

namespace engine::core {

struct TriggerComponent {
    static constexpr ecs::ComponentTypeId kComponentId = 15;

    // Reuse ColliderComponent::Shape so core stays independent of physics.
    ColliderComponent::Shape shape      = ColliderComponent::Shape::Box;
    uint8_t                  teamFilter = 0;   // 0 = any team
    uint8_t                  _pad0      = 0;
    uint8_t                  _pad1      = 0;
    uint32_t                 eventTag   = 0;   // game-mode-defined meaning
    char                     tag[32]    = {};  // designer-visible name, e.g. "objective_a"

    // Shape parameters (same layout as ColliderComponent::Params).
    ColliderComponent::Params params = { .box = {} };
};

static_assert(std::is_trivially_copyable_v<TriggerComponent>);

} // namespace engine::core
