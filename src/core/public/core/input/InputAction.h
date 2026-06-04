#pragma once
#include <cstdint>

namespace engine::core::input {

enum class InputActionType : uint8_t {
    Digital,   // on/off — key press or mouse button
    Analog1D,  // single float axis — scroll wheel, trigger, or key-as-axis
    Analog2D,  // two float axes — thumbstick or mouse look
};

struct ActionState {
    InputActionType type          = InputActionType::Digital;
    bool            held          = false;
    bool            justPressed   = false;
    bool            justReleased  = false;
    float           value         = 0.0f;   // Analog1D
    float           valueX        = 0.0f;   // Analog2D
    float           valueY        = 0.0f;   // Analog2D
};

} // namespace engine::core::input
