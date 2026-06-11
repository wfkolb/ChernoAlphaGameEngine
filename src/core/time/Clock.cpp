#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <core/time/Clock.h>

namespace engine::core::time {

uint64_t Clock::now() noexcept {
    LARGE_INTEGER counter{};
    ::QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart);
}

uint64_t Clock::ticksPerSecond() noexcept {
    LARGE_INTEGER freq{};
    ::QueryPerformanceFrequency(&freq);
    return static_cast<uint64_t>(freq.QuadPart);
}

double Clock::toSeconds(uint64_t ticks) noexcept {
    return static_cast<double>(ticks) / static_cast<double>(ticksPerSecond());
}

} // namespace engine::core::time
