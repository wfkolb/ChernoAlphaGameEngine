#pragma once
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>

namespace engine::core::log {

enum class LogLevel : uint8_t {
    Trace = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
    Fatal = 4,
};

struct LogEntry {
    LogLevel    level;
    const char* file;
    int         line;
    uint64_t    timestampUs; // microseconds since process start
    uint32_t    threadId;
    std::string message;     // pre-formatted
};

using LogDispatchFn = void(*)(const LogEntry&);

// Registered by tools::Logger::init(); falls back to stderr before that.
extern LogDispatchFn gLogFn;

// Platform helpers — not for direct call-site use.
uint64_t currentTimestampUs() noexcept;
uint32_t currentThreadId()    noexcept;

void defaultDispatch(const LogEntry& entry) noexcept; // stderr fallback

template<typename... Args>
void logImpl(LogLevel level, const char* file, int line,
             std::format_string<Args...> fmt, Args&&... args)
{
    if (!gLogFn) return;
    LogEntry e{
        level, file, line,
        currentTimestampUs(), currentThreadId(),
        std::format(fmt, std::forward<Args>(args)...)
    };
    gLogFn(e);
}

} // namespace engine::core::log

// ---------------------------------------------------------------------------
// Public macros — all engine modules use these.
// ---------------------------------------------------------------------------

#define LOG_INFO(fmt, ...) \
    ::engine::core::log::logImpl( \
        ::engine::core::log::LogLevel::Info, __FILE__, __LINE__, \
        fmt __VA_OPT__(,) __VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    ::engine::core::log::logImpl( \
        ::engine::core::log::LogLevel::Warn, __FILE__, __LINE__, \
        fmt __VA_OPT__(,) __VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    ::engine::core::log::logImpl( \
        ::engine::core::log::LogLevel::Error, __FILE__, __LINE__, \
        fmt __VA_OPT__(,) __VA_ARGS__)

// LOG_FATAL logs and then calls std::abort() unconditionally.
#define LOG_FATAL(fmt, ...) \
    do { \
        ::engine::core::log::logImpl( \
            ::engine::core::log::LogLevel::Fatal, __FILE__, __LINE__, \
            fmt __VA_OPT__(,) __VA_ARGS__); \
        std::abort(); \
    } while(0)

// LOG_TRACE is elided entirely in Release builds that have not opted in to
// DevRel instrumentation, keeping the hot path free of any overhead.
#if !defined(NDEBUG) || defined(ENGINE_DEVREL)
    #define LOG_TRACE(fmt, ...) \
        ::engine::core::log::logImpl( \
            ::engine::core::log::LogLevel::Trace, __FILE__, __LINE__, \
            fmt __VA_OPT__(,) __VA_ARGS__)
#else
    #define LOG_TRACE(fmt, ...) ((void)0)
#endif
