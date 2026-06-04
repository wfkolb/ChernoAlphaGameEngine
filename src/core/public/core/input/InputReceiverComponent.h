#pragma once
#include <core/ecs/Entity.h>
#include <cstdint>

namespace engine::core::input {

enum class FocusGroup : uint8_t {
    Gameplay,
    UI,
    Console,
    Cutscene,
};

struct InputReceiverComponent {
    static constexpr engine::core::ecs::ComponentTypeId kComponentId = 2;

    uint8_t    playerId      = 0;
    uint8_t    priority      = 0;
    bool       consumesInput = false;
    FocusGroup focusGroup    = FocusGroup::Gameplay;
};

} // namespace engine::core::input
