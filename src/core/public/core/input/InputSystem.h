#pragma once
#include "core/input/InputAction.h"
#include "core/input/InputBinding.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::core::input {

// Action-layer input system. Sits on top of the raw engine::core::InputSystem.
// Bind named actions to keys, call update() each frame, then query action states.
class InputSystem {
public:
    void bindAction(InputBinding binding);
    void clearBindingsForAction(const std::string& actionName);

    // Load [[binding]] entries from a TOML file and add them to the binding table.
    void loadFromToml(const std::filesystem::path& path);

    // Recompute all action states from the current raw InputState.
    // Must be called once per frame after engine::core::InputSystem::update().
    void update();

    bool        queryJustPressed(const std::string& actionName) const;
    bool        queryHeld(const std::string& actionName) const;
    float       queryAnalog1D(const std::string& actionName) const;
    ActionState query(const std::string& actionName) const;

private:
    std::unordered_map<std::string, std::vector<InputBinding>> bindings_;
    std::unordered_map<std::string, ActionState>               states_;
};

} // namespace engine::core::input
