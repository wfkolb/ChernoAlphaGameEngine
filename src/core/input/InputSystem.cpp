#include "core/input/InputSystem.h"
#include <core/Input.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace engine::core::input {

namespace {

bool isKeyHeld(engine::core::Key k) {
    return engine::core::InputSystem::state().isKeyDown(k);
}

bool isKeyJustPressed(engine::core::Key k) {
    return engine::core::InputSystem::state().isKeyPressed(k);
}

float getGamepadAxisValue(const engine::core::GamepadState& gs, GamepadAxis axis) {
    switch (axis) {
    case GamepadAxis::LeftStickX:   return gs.leftStickX;
    case GamepadAxis::LeftStickY:   return gs.leftStickY;
    case GamepadAxis::RightStickX:  return gs.rightStickX;
    case GamepadAxis::RightStickY:  return gs.rightStickY;
    case GamepadAxis::LeftTrigger:  return gs.leftTrigger;
    case GamepadAxis::RightTrigger: return gs.rightTrigger;
    }
    return 0.0f;
}

// XINPUT_GAMEPAD button flags matching GamepadButton enum order.
constexpr uint16_t kGamepadButtonFlags[] = {
    0x1000, // A       — XINPUT_GAMEPAD_A
    0x2000, // B       — XINPUT_GAMEPAD_B
    0x4000, // X       — XINPUT_GAMEPAD_X
    0x8000, // Y       — XINPUT_GAMEPAD_Y
    0x0100, // LeftBumper  — XINPUT_GAMEPAD_LEFT_SHOULDER
    0x0200, // RightBumper — XINPUT_GAMEPAD_RIGHT_SHOULDER
    0x0010, // Start   — XINPUT_GAMEPAD_START
    0x0020, // Back    — XINPUT_GAMEPAD_BACK
    0x0001, // DpadUp  — XINPUT_GAMEPAD_DPAD_UP
    0x0002, // DpadDown — XINPUT_GAMEPAD_DPAD_DOWN
    0x0004, // DpadLeft — XINPUT_GAMEPAD_DPAD_LEFT
    0x0008, // DpadRight — XINPUT_GAMEPAD_DPAD_RIGHT
};

bool isGamepadButtonHeld(const engine::core::GamepadState& gs, GamepadButton btn) {
    const auto idx = static_cast<uint8_t>(btn);
    if (idx >= std::size(kGamepadButtonFlags)) return false;
    return (gs.buttons & kGamepadButtonFlags[idx]) != 0;
}

} // namespace

void InputSystem::bindAction(InputBinding binding) {
    bindings_[binding.actionName].push_back(std::move(binding));
}

void InputSystem::clearBindingsForAction(const std::string& actionName) {
    bindings_.erase(actionName);
    states_.erase(actionName);
}

void InputSystem::loadFromToml(const std::filesystem::path& path) {
    for (auto& b : loadBindingsFromToml(path))
        bindAction(std::move(b));
}

void InputSystem::update() {
    states_.clear();

    const auto& rawState = engine::core::InputSystem::state();

    for (auto& [name, bindings] : bindings_) {
        ActionState& s = states_[name];
        if (!bindings.empty())
            s.type = bindings[0].actionType;

        for (const auto& b : bindings) {
            switch (b.sourceType) {
            case SourceType::Key:
                switch (b.actionType) {
                case InputActionType::Digital:
                    if (isKeyHeld(b.key))        s.held        = true;
                    if (isKeyJustPressed(b.key)) s.justPressed = true;
                    break;
                case InputActionType::Analog1D:
                    if (isKeyHeld(b.key)) s.value += b.scale;
                    break;
                case InputActionType::Analog2D:
                    if (isKeyHeld(b.key)) s.valueX += b.scale;
                    break;
                }
                break;

            case SourceType::MouseAxis:
                // Mouse axis handled externally via InputState mouse deltas.
                break;

            case SourceType::GamepadAxis: {
                const int slot = std::clamp(static_cast<int>(b.gamepadIndex), 0,
                                            engine::core::InputState::kMaxGamepads - 1);
                const auto& gs = rawState.gamepad(slot);
                if (!gs.connected) break;
                const float axisVal = getGamepadAxisValue(gs, b.gamepadAxis) * b.scale;
                switch (b.actionType) {
                case InputActionType::Analog1D:
                    s.value += axisVal;
                    break;
                case InputActionType::Analog2D:
                    s.valueX += axisVal;
                    break;
                case InputActionType::Digital:
                    if (std::abs(axisVal) > 0.5f) { s.held = true; s.justPressed = true; }
                    break;
                }
                break;
            }

            case SourceType::GamepadButton: {
                const int slot = std::clamp(static_cast<int>(b.gamepadIndex), 0,
                                            engine::core::InputState::kMaxGamepads - 1);
                const auto& gs = rawState.gamepad(slot);
                if (!gs.connected) break;
                const bool held = isGamepadButtonHeld(gs, b.gamepadButton);
                switch (b.actionType) {
                case InputActionType::Digital:
                    if (held) { s.held = true; s.justPressed = true; }
                    break;
                case InputActionType::Analog1D:
                    if (held) s.value += b.scale;
                    break;
                case InputActionType::Analog2D:
                    if (held) s.valueX += b.scale;
                    break;
                }
                break;
            }
            }
        }
    }
}

bool InputSystem::queryJustPressed(const std::string& actionName) const {
    auto it = states_.find(actionName);
    return it != states_.end() && it->second.justPressed;
}

bool InputSystem::queryHeld(const std::string& actionName) const {
    auto it = states_.find(actionName);
    return it != states_.end() && it->second.held;
}

float InputSystem::queryAnalog1D(const std::string& actionName) const {
    auto it = states_.find(actionName);
    return it != states_.end() ? it->second.value : 0.0f;
}

ActionState InputSystem::query(const std::string& actionName) const {
    auto it = states_.find(actionName);
    return it != states_.end() ? it->second : ActionState{};
}

} // namespace engine::core::input
