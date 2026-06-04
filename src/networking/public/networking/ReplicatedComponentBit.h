#pragma once
#include <cstdint>

namespace engine::networking {

enum ReplicatedComponentBit : uint32_t {
    RCB_TRANSFORM       = 1u << 0,
    RCB_RIGID_BODY      = 1u << 1,
    RCB_HEALTH          = 1u << 2,
    RCB_WEAPON_STATE    = 1u << 3,
    RCB_ANIMATION_STATE = 1u << 4,
};

} // namespace engine::networking
