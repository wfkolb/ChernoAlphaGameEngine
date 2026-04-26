#pragma once
#include <core/log.h>

namespace engine::tools {

class Logger {
public:
    static void init(::engine::core::log::LogLevel minLevel =
                         ::engine::core::log::LogLevel::Trace);
    static void shutdown();
    static void dispatch(const ::engine::core::log::LogEntry& entry);
    static void setLevel(::engine::core::log::LogLevel level) noexcept;

private:
    Logger() = delete;
};

} // namespace engine::tools
