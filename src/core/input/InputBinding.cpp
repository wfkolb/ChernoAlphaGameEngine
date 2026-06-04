#include "core/input/InputBinding.h"
#include <core/log.h>
#include <toml++/toml.hpp>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace engine::core::input {

namespace {

using engine::core::Key;

const std::unordered_map<std::string_view, Key> kKeyNames = {
    // Letters
    {"A", Key::A}, {"B", Key::B}, {"C", Key::C}, {"D", Key::D}, {"E", Key::E},
    {"F", Key::F}, {"G", Key::G}, {"H", Key::H}, {"I", Key::I}, {"J", Key::J},
    {"K", Key::K}, {"L", Key::L}, {"M", Key::M}, {"N", Key::N}, {"O", Key::O},
    {"P", Key::P}, {"Q", Key::Q}, {"R", Key::R}, {"S", Key::S}, {"T", Key::T},
    {"U", Key::U}, {"V", Key::V}, {"W", Key::W}, {"X", Key::X}, {"Y", Key::Y},
    {"Z", Key::Z},
    // Digits
    {"0", Key::D0}, {"1", Key::D1}, {"2", Key::D2}, {"3", Key::D3}, {"4", Key::D4},
    {"5", Key::D5}, {"6", Key::D6}, {"7", Key::D7}, {"8", Key::D8}, {"9", Key::D9},
    // Special
    {"Escape",    Key::Escape},   {"Enter",    Key::Return},  {"Return",  Key::Return},
    {"Space",     Key::Space},    {"Tab",      Key::Tab},     {"BackSpace",Key::BackSpace},
    {"LShift",    Key::LShift},   {"RShift",   Key::RShift},
    {"LCtrl",     Key::LCtrl},    {"RCtrl",    Key::RCtrl},
    {"LAlt",      Key::LAlt},     {"RAlt",     Key::RAlt},
    // Arrow keys
    {"Up",    Key::Up},   {"Down",  Key::Down},
    {"Left",  Key::Left}, {"Right", Key::Right},
    // Function keys
    {"F1",  Key::F1},  {"F2",  Key::F2},  {"F3",  Key::F3},  {"F4",  Key::F4},
    {"F5",  Key::F5},  {"F6",  Key::F6},  {"F7",  Key::F7},  {"F8",  Key::F8},
    {"F9",  Key::F9},  {"F10", Key::F10}, {"F11", Key::F11}, {"F12", Key::F12},
    // Mouse buttons
    {"MouseLeft", Key::MouseLeft}, {"MouseRight", Key::MouseRight},
    {"MouseMiddle", Key::MouseMiddle},
};

Key parseKey(std::string_view s, const std::filesystem::path& src) {
    auto it = kKeyNames.find(s);
    if (it == kKeyNames.end()) {
        LOG_WARN("InputBinding: unknown key '{}' in '{}'", s, src.string());
        return Key::Unknown;
    }
    return it->second;
}

InputActionType parseActionType(std::string_view s, const std::filesystem::path& src) {
    if (s == "Digital")  return InputActionType::Digital;
    if (s == "Analog1D") return InputActionType::Analog1D;
    if (s == "Analog2D") return InputActionType::Analog2D;
    LOG_WARN("InputBinding: unknown action type '{}' in '{}'", s, src.string());
    return InputActionType::Digital;
}

std::optional<toml::table> tryParseFile(const std::filesystem::path& path) {
    try {
        return toml::parse_file(path.string());
    } catch (const toml::parse_error& e) {
        LOG_WARN("InputBinding: failed to parse '{}': {}", path.string(), e.what());
        return std::nullopt;
    }
}

} // namespace

std::vector<InputBinding> loadBindingsFromToml(const std::filesystem::path& path) {
    std::vector<InputBinding> out;

    auto parsed = tryParseFile(path);
    if (!parsed) return out;

    auto* arr = (*parsed)["binding"].as_array();
    if (!arr) return out;

    for (auto& elem : *arr) {
        auto* t = elem.as_table();
        if (!t) continue;

        InputBinding b;
        if (auto v = (*t)["action"].value<std::string>()) b.actionName = *v;
        if (auto v = (*t)["type"].value<std::string>())   b.actionType = parseActionType(*v, path);
        if (auto v = (*t)["key"].value<std::string>())    b.key        = parseKey(*v, path);
        if (auto v = (*t)["scale"].value<double>())       b.scale      = static_cast<float>(*v);

        if (!b.actionName.empty())
            out.push_back(std::move(b));
    }

    return out;
}

} // namespace engine::core::input
