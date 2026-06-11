#pragma once
#include <cstdint>

namespace engine::core {

#if !defined(NDEBUG) || defined(ENGINE_DEVREL)
struct ProfilerScope {
    explicit ProfilerScope(const char* name) noexcept;
    ~ProfilerScope() noexcept;
private:
    const char* name_;
    uint64_t    startTick_;
};
#endif

} // namespace engine::core

// CPU scope macro: expands in Debug and DevRel; elided in Release.
#if !defined(NDEBUG) || defined(ENGINE_DEVREL)
#  define PROFILE_SCOPE(name) ::engine::core::ProfilerScope __prof##__LINE__{name}
#else
#  define PROFILE_SCOPE(name) ((void)0)
#endif
