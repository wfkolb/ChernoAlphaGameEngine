# Tools: Logging and Configuration System

Status: Approved (Phase 2)
Owner: Tools Lead
Task: #14
References: architecture.md §8, coding-standards.md §11, scope-tools.md

---

## 1. Logger Architecture

### 1.1 Overview

The logger has three layers:

```
LOG_INFO("msg {}", arg)
      ↓  (macro, compile-time level check)
core::log::logImpl(level, file, line, fmt, args...)
      ↓  (calls the registered function pointer — no link-time dependency on tools)
tools::Logger::dispatch(entry)
      ↓
  ┌──────────────┐    ┌──────────────┐
  │ ConsoleSink  │    │  FileSink    │
  │ VT-100 color │    │ rotating txt │
  └──────────────┘    └──────────────┘
```

The separation between `core/log.h` (the macro frontend) and `tools::Logger` (the implementation) allows `core` to log without a link-time dependency on `tools`. The function pointer is registered by `tools::Logger::init()` at application startup (step 1 in `BootstrapOrder.cpp`).

### 1.2 Log entry

```cpp
struct LogEntry {
    LogLevel    level;
    const char* file;      // __FILE__
    int         line;      // __LINE__
    uint64_t    timestampUs;  // microseconds since logger init
    uint32_t    threadId;
    std::string message;   // pre-formatted by std::format
};
```

### 1.3 Frontend: `core/log.h`

```cpp
// core/log.h — includable from any module including core itself
namespace engine::core::log {
    enum class LogLevel : uint8_t { Trace=0, Info, Warn, Error, Fatal };

    // Function pointer registered by tools::Logger at startup.
    // Default implementation: stderr only (used before tools::Logger is initialized).
    using LogFn = void(*)(const LogEntry&);
    extern LogFn gLogFn;

    template<typename... Args>
    void logImpl(LogLevel level, const char* file, int line,
                 std::format_string<Args...> fmt, Args&&... args) {
        if (gLogFn) {
            LogEntry entry{ level, file, line, currentTimestampUs(), currentThreadId(),
                            std::format(fmt, std::forward<Args>(args)...) };
            gLogFn(entry);
        }
    }
}

#define LOG_TRACE(fmt, ...) ::engine::core::log::logImpl(::engine::core::log::LogLevel::Trace, __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO(fmt, ...)  ::engine::core::log::logImpl(::engine::core::log::LogLevel::Info,  __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN(fmt, ...)  ::engine::core::log::logImpl(::engine::core::log::LogLevel::Warn,  __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_ERROR(fmt, ...) ::engine::core::log::logImpl(::engine::core::log::LogLevel::Error, __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_FATAL(fmt, ...) do { ::engine::core::log::logImpl(::engine::core::log::LogLevel::Fatal, __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__); std::abort(); } while(0)
```

### 1.4 Compile-time level elision

`LOG_TRACE` expands to nothing in Release builds:

```cpp
#if defined(NDEBUG) && !defined(ENGINE_DEVREL)
    #define LOG_TRACE(fmt, ...) ((void)0)
#endif
```

`LOG_INFO` and above are never elided (they carry real operational value in Release).

---

## 2. Logger Implementation (`tools::Logger`)

### 2.1 Sinks

```cpp
class Logger {
public:
    static void init(LogLevel minLevel = LogLevel::Trace);
    static void shutdown();

    // Called by tools::Logger::init() to register with core::log::gLogFn
    static void dispatch(const LogEntry& entry);

private:
    static void consoleSink(const LogEntry& entry);
    static void fileSink(const LogEntry& entry);

    static std::mutex mutex_;           // serializes all sink writes
    static LogLevel   minLevel_;
    static FILE*      logFile_;
};
```

### 2.2 Thread safety

All sink writes are serialized by a single `std::mutex`. For high-throughput scenarios (> 250k msgs/s target from scope-tools.md), the mutex contention is acceptable because `LOG_TRACE` is elided in Release and the remaining macros are not on the hot path. v2 may move to a lock-free MPSC queue if profiling shows contention.

### 2.3 Console sink with VT-100 color

Enabled on Windows 10+ by calling `SetConsoleMode(handle, ... | ENABLE_VIRTUAL_TERMINAL_PROCESSING)` in `Logger::init()`. Degrades gracefully if VT processing is not available (writes plain text).

| Level | Color code | Prefix |
|---|---|---|
| Trace | `\033[90m` (dark gray) | `[TRACE]` |
| Info  | `\033[0m`  (default)   | `[INFO ]` |
| Warn  | `\033[93m` (bright yellow) | `[WARN ]` |
| Error | `\033[91m` (bright red)    | `[ERROR]` |
| Fatal | `\033[95m` (bright magenta) | `[FATAL]` |

Format: `[LEVEL] HH:MM:SS.mmm [file:line] message\n`

### 2.4 File sink with rotation

Log files are written to `%LOCALAPPDATA%\<engine>\logs\`. Directory is created if absent.

File naming: `engine-YYYYMMDD-HHMMSS.log` (timestamp at session start).

Rotation policy:
- **Per-session**: a new file is created on each `Logger::init()` call.
- **Size limit**: if the current log file exceeds `kMaxLogFileSizeBytes = 10 * 1024 * 1024` (10 MB), close it and open a new file with `_part2`, `_part3`, etc., suffix.
- **File retention**: on init, delete log files older than `kLogRetentionDays = 7` days.

File format: plain UTF-8 text, no color codes, otherwise identical format to console.

---

## 3. Configuration System (`tools::Config`)

### 3.1 TOML schema

The authoritative schema lives in `engine.toml` alongside the executable. Sections and keys:

```toml
[engine]
version = "0.1.0"   # read-only; written by build system, never by user

[render]
width  = 1280
height = 720
vsync  = true
fovDegrees = 60.0
shadowSplitLambda = 0.95
shadowMapResolution = 2048
uploadHeapSizeMb = 4
anisotropy = 8

[network]
role = "none"           # "none", "server", "client"
host = "127.0.0.1"
port = 7777
tickRate = 30
timeoutMs = 5000
rewindWindowTicks = 6
interpolationDelayTicks = 2

[editor]
enabled = true          # runtime override; always false in Release

[log]
minLevel = "info"       # "trace", "info", "warn", "error"
retentionDays = 7
maxFileSizeMb = 10
```

### 3.2 Loading order

1. Load `<exe_dir>/engine.toml` (default values; shipped with the game).
2. Load `%APPDATA%\<engine>\engine.toml` (user overrides; created lazily). Keys in user override take precedence.
3. If either file is absent, it is silently ignored and defaults are used.

`Config::init()` returns `Result<void>` (failure only if a file exists but is malformed TOML — logged at `LOG_ERROR`; process continues with defaults).

### 3.3 Typed accessor API

```cpp
// tools/Config.h
class Config {
public:
    static void init();                                  // call once at startup
    static void shutdown();

    static int32_t     getInt   (std::string_view section, std::string_view key, int32_t     defaultVal);
    static float       getFloat (std::string_view section, std::string_view key, float       defaultVal);
    static bool        getBool  (std::string_view section, std::string_view key, bool        defaultVal);
    static std::string getString(std::string_view section, std::string_view key, std::string_view defaultVal);
};
```

**Read-once at startup**. Subsystems call `Config::getXxx()` during their `init()` phase and cache the result in their own member variables. There is no live-reload of config values (except shader hot-reload, which is separate). No long-lived `Config*` is handed out.

### 3.4 No config types in public headers

`toml++` types (`toml::table`, `toml::value`, etc.) never appear in `tools/Config.h`. They are implementation details of `Config.cpp`. Callers receive plain C++ values (`int32_t`, `float`, `bool`, `std::string`).

---

## 4. Runtime Level Filter

`Logger::init()` reads `[log].minLevel` from config and sets `minLevel_`. Messages below `minLevel_` are discarded before formatting. This is a **runtime** filter; `LOG_TRACE` is still a **compile-time** elision in Release.

```
Release build: LOG_TRACE → nothing (compile-time)
               LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL: runtime filter applies
Debug/DevRel:  all levels compile; runtime filter applies
```

The `minLevel_` can be changed at runtime via `Logger::setLevel(LogLevel)` for development use — not exposed through config after startup.

---

## 5. File Layout

```
src/tools/
├── public/tools/
│   ├── Logger.h        — Logger::init/shutdown/dispatch; LogLevel enum
│   └── Config.h        — Config::init/getXxx
└── internal/
    ├── LoggerImpl.h    — ConsoleSink, FileSink, rotation details
    └── ConfigImpl.h    — toml++ parsing, schema validation
src/core/
└── public/core/
    └── log.h           — LOG_* macros, gLogFn, LogEntry, LogLevel
```

`core/log.h` includes no tools headers. `tools/Logger.h` may include `core/log.h` for `LogEntry` and `LogLevel`.
