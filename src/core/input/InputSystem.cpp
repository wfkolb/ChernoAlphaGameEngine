#include "core/input/InputSystem.h"
#include <core/Input.h>

namespace engine::core::input {

namespace {

bool isKeyHeld(engine::core::Key k) {
    return engine::core::InputSystem::state().isKeyDown(k);
}

bool isKeyJustPressed(engine::core::Key k) {
    return engine::core::InputSystem::state().isKeyPressed(k);
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

    for (auto& [name, bindings] : bindings_) {
        ActionState& s = states_[name];
        if (!bindings.empty())
            s.type = bindings[0].actionType;

        for (const auto& b : bindings) {
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
