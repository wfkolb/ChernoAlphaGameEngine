#pragma once
#include <cstdint>

namespace engine::core::input {

// Per-tick snapshot of client input, sent to the server each game tick.
struct InputFrame {
    uint32_t tick               = 0;
    uint64_t clientTimestamp    = 0;
    uint64_t digitalHeld        = 0;        // bitmask: up to 64 digital actions
    uint64_t digitalJustPressed = 0;        // bitmask: actions pressed this tick
    float    lookYawDelta       = 0.0f;
    float    lookPitchDelta     = 0.0f;     // clamped to ±89° on client before send
};

} // namespace engine::core::input
