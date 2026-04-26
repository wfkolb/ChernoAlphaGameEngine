#include <tools/Logger.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shlobj.h>   // SHGetFolderPathW / CSIDL_LOCAL_APPDATA

namespace engine::tools {

using LogLevel = ::engine::core::log::LogLevel;
using LogEntry = ::engine::core::log::LogEntry;

// ---------------------------------------------------------------------------
// Module-private state
// ---------------------------------------------------------------------------

static std::mutex mutex_;
static LogLevel   minLevel_  = LogLevel::Trace;
static FILE*      logFile_   = nullptr;
static int        partNumber_ = 1; // rotation suffix counter

static constexpr uint64_t kMaxLogFileSizeBytes = 10ULL * 1024ULL * 1024ULL;
static constexpr int      kLogRetentionDays    = 7;

// Saved so we can open rotated parts with the same base name.
static std::wstring logFilePath_;

// ---------------------------------------------------------------------------
// Internal helpers
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

static const char* levelColor(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace: return "\033[90m";  // dark gray
        case LogLevel::Info:  return "\033[0m";   // default
        case LogLevel::Warn:  return "\033[93m";  // bright yellow
        case LogLevel::Error: return "\033[91m";  // bright red
        case LogLevel::Fatal: return "\033[95m";  // bright magenta
    }
    return "\033[0m";
}

// Decomposes microseconds into wall-clock-relative hours/mins/secs/ms.
struct TimeComponents {
    int hours;
    int minutes;
    int seconds;
    int milliseconds;
};

static TimeComponents decompose(uint64_t timestampUs) noexcept
{
    uint64_t totalMs  = timestampUs / 1'000ULL;
    uint64_t totalSec = totalMs     / 1'000ULL;
    return {
        static_cast<int>((totalSec / 3600ULL) % 24ULL),
        static_cast<int>((totalSec / 60ULL)   % 60ULL),
        static_cast<int>( totalSec             % 60ULL),
        static_cast<int>( totalMs              % 1'000ULL),
    };
}

// ---------------------------------------------------------------------------
// File rotation
// ---------------------------------------------------------------------------

static void openLogFilePart()
{
    // Build a path like: <base>_part2.log, <base>_part3.log, etc.
    // logFilePath_ already ends in ".log"; insert suffix before that.
    std::wstring path = logFilePath_;
    if (partNumber_ > 1) {
        auto dotPos = path.rfind(L'.');
        if (dotPos != std::wstring::npos)
            path.insert(dotPos, L"_part" + std::to_wstring(partNumber_));
    }
    if (logFile_) {
        std::fflush(logFile_);
        std::fclose(logFile_);
        logFile_ = nullptr;
    }
    _wfopen_s(&logFile_, path.c_str(), L"at,ccs=UTF-8");
}

static void maybeRotate()
{
    if (!logFile_) return;
    const long pos = std::ftell(logFile_);
    if (pos >= 0 && static_cast<uint64_t>(pos) >= kMaxLogFileSizeBytes) {
        ++partNumber_;
        openLogFilePart();
    }
}

// ---------------------------------------------------------------------------
// Old log file cleanup
// ---------------------------------------------------------------------------

static void deleteOldLogs(const std::wstring& logDir)
{
    std::wstring pattern = logDir + L"\\engine-*.log";
    WIN32_FIND_DATAW ffd{};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME nowSt{};
    GetSystemTime(&nowSt);
    FILETIME nowFt{};
    SystemTimeToFileTime(&nowSt, &nowFt);
    ULARGE_INTEGER nowUli{ nowFt.dwLowDateTime, nowFt.dwHighDateTime };

    // 100-ns intervals per day
    constexpr uint64_t kIntervalsPerDay = 864'000'000'000ULL;
    const uint64_t cutoff = nowUli.QuadPart - kLogRetentionDays * kIntervalsPerDay;

    do {
        ULARGE_INTEGER fileTime{
            ffd.ftLastWriteTime.dwLowDateTime,
            ffd.ftLastWriteTime.dwHighDateTime
        };
        if (fileTime.QuadPart < cutoff) {
            std::wstring full = logDir + L'\\' + ffd.cFileName;
            DeleteFileW(full.c_str());
        }
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);
}

// ---------------------------------------------------------------------------
// Sinks
// ---------------------------------------------------------------------------

static void consoleSink(const LogEntry& entry)
{
    const TimeComponents tc = decompose(entry.timestampUs);

    // stderr output uses VT-100 color sequences enabled in init().
    std::fprintf(stderr,
        "%s[%s] %02d:%02d:%02d.%03d [%s:%d] %s\033[0m\n",
        levelColor(entry.level),
        levelLabel(entry.level),
        tc.hours, tc.minutes, tc.seconds, tc.milliseconds,
        entry.file,
        entry.line,
        entry.message.c_str());
}

static void fileSink(const LogEntry& entry)
{
    if (!logFile_) return;

    maybeRotate();
    if (!logFile_) return;

    const TimeComponents tc = decompose(entry.timestampUs);

    std::fprintf(logFile_,
        "[%s] %02d:%02d:%02d.%03d [%s:%d] %s\n",
        levelLabel(entry.level),
        tc.hours, tc.minutes, tc.seconds, tc.milliseconds,
        entry.file,
        entry.line,
        entry.message.c_str());

    // Flush WARN and above immediately so crashes don't lose context.
    if (entry.level >= LogLevel::Warn)
        std::fflush(logFile_);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Logger::init(LogLevel minLevel)
{
    std::lock_guard lock(mutex_);

    minLevel_   = minLevel;
    partNumber_ = 1;

    // Enable VT-100 processing so color sequences render in the Windows console.
    HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);
    if (hStderr != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hStderr, &mode))
            SetConsoleMode(hStderr, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    // Resolve %LOCALAPPDATA%\engine\logs
    wchar_t localAppData[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT,
                     localAppData);

    std::wstring logDir = std::wstring(localAppData) + L"\\engine\\logs";
    CreateDirectoryW((std::wstring(localAppData) + L"\\engine").c_str(), nullptr);
    CreateDirectoryW(logDir.c_str(), nullptr);

    deleteOldLogs(logDir);

    // Build a timestamped filename for this session.
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t nameBuf[64]{};
    std::swprintf(nameBuf, 64, L"engine-%04d%02d%02d-%02d%02d%02d.log",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    logFilePath_ = logDir + L'\\' + nameBuf;
    openLogFilePart();

    // Register this dispatch function as the global sink.
    ::engine::core::log::gLogFn = &Logger::dispatch;
}

void Logger::shutdown()
{
    std::lock_guard lock(mutex_);

    if (logFile_) {
        std::fflush(logFile_);
        std::fclose(logFile_);
        logFile_ = nullptr;
    }

    // Restore the stderr fallback so any post-shutdown log calls still surface.
    ::engine::core::log::gLogFn = &::engine::core::log::defaultDispatch;
}

void Logger::dispatch(const LogEntry& entry)
{
    std::lock_guard lock(mutex_);

    if (entry.level < minLevel_) return;

    consoleSink(entry);
    fileSink(entry);
}

void Logger::setLevel(LogLevel level) noexcept
{
    // Serialize with dispatch() so a level change never races a concurrent log call.
    std::lock_guard lock(mutex_);
    minLevel_ = level;
}

} // namespace engine::tools
