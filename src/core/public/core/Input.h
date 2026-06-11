#pragma once
#include <cstdint>
#include <array>

namespace engine::core {

// Key codes — values match Windows VK_ codes where possible.
enum class Key : uint16_t {
    Unknown = 0,
    // Letters
    A=0x41,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,
    // Digits
    D0=0x30,D1,D2,D3,D4,D5,D6,D7,D8,D9,
    // Special
    Escape=0x1B, Return=0x0D, Space=0x20, Tab=0x09, BackSpace=0x08,
    LShift=0xA0, RShift=0xA1, LCtrl=0xA2, RCtrl=0xA3, LAlt=0xA4, RAlt=0xA5,
    Up=0x26, Down=0x28, Left=0x25, Right=0x27,
    F1=0x70,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,F12,
    // Mouse buttons (match VK_LBUTTON/VK_RBUTTON/VK_MBUTTON)
    MouseLeft=0x01, MouseRight=0x02, MouseMiddle=0x04,
};

// Input events — published on the EventBus.
struct KeyEvent {
    Key  key;
    bool pressed;  // true = down, false = up
    bool repeat;   // true if OS auto-repeated
};

struct MouseMoveEvent {
    int32_t deltaX, deltaY;  // raw delta (RawInput, no acceleration)
};

struct MouseScrollEvent {
    int32_t delta;  // positive = up, negative = down, units of WHEEL_DELTA
};

// Gamepad state for a single controller slot.
struct GamepadState {
    bool    connected    = false;
    float   leftStickX   = 0.0f;  // [-1, 1] after dead-zone
    float   leftStickY   = 0.0f;  // [-1, 1] after dead-zone
    float   rightStickX  = 0.0f;  // [-1, 1] after dead-zone
    float   rightStickY  = 0.0f;  // [-1, 1] after dead-zone
    float   leftTrigger  = 0.0f;  // [0, 1] after dead-zone
    float   rightTrigger = 0.0f;  // [0, 1] after dead-zone
    uint16_t buttons     = 0;     // XINPUT_GAMEPAD button bitmask
};

// Snapshot of current frame's input. Read-only for game systems.
// Updated by InputSystem::update() before game logic runs.
class InputState {
public:
    bool isKeyDown(Key k) const;
    bool isKeyPressed(Key k) const;   // down this frame, not last frame
    bool isKeyReleased(Key k) const;  // up this frame, was down last frame

    int32_t mouseDeltaX() const { return mouseDeltaX_; }
    int32_t mouseDeltaY() const { return mouseDeltaY_; }
    int32_t mouseScrollDelta() const { return mouseScrollDelta_; }

    // Gamepad state for controller slots 0-3.
    static constexpr int kMaxGamepads = 4;
    const GamepadState& gamepad(int slot) const { return gamepads_[slot]; }

private:
    friend class InputSystem;
    static constexpr int kKeyCount = 256;
    std::array<bool, kKeyCount> curr_ = {};
    std::array<bool, kKeyCount> prev_ = {};
    int32_t mouseDeltaX_      = 0;
    int32_t mouseDeltaY_      = 0;
    int32_t mouseScrollDelta_ = 0;
    std::array<GamepadState, kMaxGamepads> gamepads_ = {};
};

// Processes Win32 WM_INPUT messages and maintains InputState.
// Call registerRawInput() once after the window is created.
// Call update() each frame before game logic.
class InputSystem {
public:
    // Register RawInput devices (keyboard + mouse).
    // hwnd: HWND of the game window, passed as void* to avoid pulling in Windows.h.
    static bool registerRawInput(void* hwnd);

    // Call from the Win32 message handler on WM_INPUT.
    // hRawInput: the HRAWINPUT from WM_INPUT lParam, as void*.
    static void processRawInput(void* hRawInput);

    // Advance prev_ = curr_, flush pending deltas. Call once per frame.
    static void update();

    static const InputState& state() { return state_; }

private:
    static InputState state_;
    static int32_t    pendingDeltaX_;
    static int32_t    pendingDeltaY_;
    static int32_t    pendingScroll_;
};

} // namespace engine::core
