#pragma once
#include <networking/InputMessage.h>
#include <core/ecs/Entity.h>

namespace engine::networking {

// ECS component placed on server-side player entities to store the most
// recently received network input.  The server simulation reads this each
// tick via the ECS instead of InputReceiverComponent (which is local-only).
struct NetworkInputComponent {
    static constexpr engine::core::ecs::ComponentTypeId kComponentId = 19;

    InputMessage lastNetworkInput {};
};

} // namespace engine::networking
