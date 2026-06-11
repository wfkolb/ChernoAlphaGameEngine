#pragma once
#include <cstdint>

namespace engine::core::time {

struct Clock {
    // Returns current time as raw QPC ticks (monotonic). Use toSeconds() to convert.
    static uint64_t now() noexcept;

    // Returns the number of ticks per second (QPC frequency).
    static uint64_t ticksPerSecond() noexcept;

    // Converts a tick count to seconds.
    static double toSeconds(uint64_t ticks) noexcept;
};

} // namespace engine::core::time
