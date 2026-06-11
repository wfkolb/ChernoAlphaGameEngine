#pragma once
#include <core/Input.h>
#include "core/input/InputAction.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::core::input {

enum class SourceType : uint8_t {
    Key,
    MouseAxis,
    GamepadAxis,
    GamepadButton,
};

enum class GamepadAxis : uint8_t {
    LeftStickX,
    LeftStickY,
    RightStickX,
    RightStickY,
    LeftTrigger,
    RightTrigger,
};

enum class GamepadButton : uint8_t {
    A, B, X, Y,
    LeftBumper, RightBumper,
    Start, Back,
    DpadUp, DpadDown, DpadLeft, DpadRight,
};

struct InputBinding {
    std::string          actionName;
    InputActionType      actionType  = InputActionType::Digital;
    SourceType           sourceType  = SourceType::Key;
    engine::core::Key    key         = engine::core::Key::Unknown;
    GamepadAxis          gamepadAxis = GamepadAxis::LeftStickX;
    GamepadButton        gamepadButton = GamepadButton::A;
    uint8_t              gamepadIndex  = 0;   // controller slot 0-3
    float                scale         = 1.0f;
};

// Parses [[binding]] array entries from a TOML file.
// Returns an empty vector and logs a warning on parse failure.
std::vector<InputBinding> loadBindingsFromToml(const std::filesystem::path& path);

} // namespace engine::core::input
