#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace engine::tools {

class Config {
public:
    static void init();     // loads engine.toml + user override
    static void shutdown();

    static int32_t     getInt   (std::string_view section, std::string_view key, int32_t          defaultVal);
    static float       getFloat (std::string_view section, std::string_view key, float             defaultVal);
    static bool        getBool  (std::string_view section, std::string_view key, bool              defaultVal);
    static std::string getString(std::string_view section, std::string_view key, std::string_view  defaultVal);

private:
    Config() = delete;
};

} // namespace engine::tools
