#pragma once
#include <core/Input.h>
#include "core/input/InputAction.h"
#include <filesystem>
#include <string>
#include <vector>

namespace engine::core::input {

struct InputBinding {
    std::string          actionName;
    InputActionType      actionType = InputActionType::Digital;
    engine::core::Key    key        = engine::core::Key::Unknown;
    float                scale      = 1.0f;
};

// Parses [[binding]] array entries from a TOML file.
// Returns an empty vector and logs a warning on parse failure.
std::vector<InputBinding> loadBindingsFromToml(const std::filesystem::path& path);

} // namespace engine::core::input
