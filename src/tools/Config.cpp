#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>

#include <tools/Config.h>
#include <core/log.h>

#include <toml++/toml.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace engine::tools {

namespace {

std::optional<toml::table> g_exeConfig;
std::optional<toml::table> g_userConfig;

std::optional<toml::table> tryParseFile(const std::filesystem::path& path)
{
    try {
        return toml::parse_file(path.string());
    } catch (const toml::parse_error& e) {
        LOG_ERROR("Config: failed to parse '{}': {}", path.string(), e.what());
        return std::nullopt;
    }
}

template<typename T>
T lookupValue(std::string_view section, std::string_view key, T defaultVal)
{
    // User config takes precedence.
    if (g_userConfig) {
        if (auto* tbl = g_userConfig->get_as<toml::table>(section)) {
            if (auto v = tbl->get_as<T>(key)) {
                return v->get();
            }
        }
    }
    // Fall back to exe-dir config.
    if (g_exeConfig) {
        if (auto* tbl = g_exeConfig->get_as<toml::table>(section)) {
            if (auto v = tbl->get_as<T>(key)) {
                return v->get();
            }
        }
    }
    return defaultVal;
}

} // namespace

void Config::init()
{
    // Resolve exe directory.
    wchar_t exePathBuf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
    std::filesystem::path exeToml =
        std::filesystem::path(exePathBuf).parent_path() / L"engine.toml";

    g_exeConfig = tryParseFile(exeToml);

    // Resolve %APPDATA%\engine\engine.toml (CSIDL_APPDATA = Roaming AppData).
    wchar_t appDataBuf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appDataBuf))) {
        std::filesystem::path userToml =
            std::filesystem::path(appDataBuf) / L"engine" / L"engine.toml";
        g_userConfig = tryParseFile(userToml);
    }
}

void Config::shutdown()
{
    g_exeConfig.reset();
    g_userConfig.reset();
}

int32_t Config::getInt(std::string_view section, std::string_view key, int32_t defaultVal)
{
    return static_cast<int32_t>(lookupValue<int64_t>(section, key, static_cast<int64_t>(defaultVal)));
}

float Config::getFloat(std::string_view section, std::string_view key, float defaultVal)
{
    return static_cast<float>(lookupValue<double>(section, key, static_cast<double>(defaultVal)));
}

bool Config::getBool(std::string_view section, std::string_view key, bool defaultVal)
{
    return lookupValue<bool>(section, key, defaultVal);
}

std::string Config::getString(std::string_view section, std::string_view key,
                              std::string_view defaultVal)
{
    return lookupValue<std::string>(section, key, std::string(defaultVal));
}

} // namespace engine::tools
