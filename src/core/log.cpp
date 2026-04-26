#include <core/log.h>

#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace engine::core::log {

// ---------------------------------------------------------------------------
// Timestamp
// ---------------------------------------------------------------------------

// QPC frequency is constant after the first query; cache it once.
static uint64_t qpcFrequency() noexcept
{
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    return static_cast<uint64_t>(freq.QuadPart);
}

static uint64_t qpcOrigin() noexcept
{
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return static_cast<uint64_t>(t.QuadPart);
}

// Both statics are initialised once at process start.
static const uint64_t kQpcFreq   = qpcFrequency();
static const uint64_t kQpcOrigin = qpcOrigin();

uint64_t currentTimestampUs() noexcept
{
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const uint64_t elapsed = static_cast<uint64_t>(now.QuadPart) - kQpcOrigin;
    // Multiply first to preserve precision; frequency is typically ~10 MHz.
    return (elapsed * 1'000'000ULL) / kQpcFreq;
}

// ---------------------------------------------------------------------------
// Thread ID
// ---------------------------------------------------------------------------

uint32_t currentThreadId() noexcept
{
    return static_cast<uint32_t>(GetCurrentThreadId());
}

// ---------------------------------------------------------------------------
// Default (stderr) dispatch — active before tools::Logger::init() is called.
// ---------------------------------------------------------------------------

static const char* levelLabel(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "?????";
}

void defaultDispatch(const LogEntry& entry) noexcept
{
    std::fprintf(stderr, "[%s] %s:%d %s\n",
        levelLabel(entry.level),
        entry.file,
        entry.line,
        entry.message.c_str());
}

// ---------------------------------------------------------------------------
// Global function pointer — initialised to the stderr fallback so that any
// log call issued before tools::Logger::init() still produces output.
// ---------------------------------------------------------------------------

LogDispatchFn gLogFn = &defaultDispatch;

} // namespace engine::core::log
