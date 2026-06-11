#include <core/Profiler.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace engine::core {

#if !defined(NDEBUG) || defined(ENGINE_DEVREL)
ProfilerScope::ProfilerScope(const char* name) noexcept : name_(name) {
    LARGE_INTEGER li;
    ::QueryPerformanceCounter(&li);
    startTick_ = static_cast<uint64_t>(li.QuadPart);
}

ProfilerScope::~ProfilerScope() noexcept {
    (void)name_;
    (void)startTick_;
}
#endif

} // namespace engine::core
