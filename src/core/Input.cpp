#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <hidusage.h>

#include <memory>

#include "core/Input.h"
#include "core/diag/Assert.h"

namespace engine::core {

InputState InputSystem::state_{};
int32_t    InputSystem::pendingDeltaX_ = 0;
int32_t    InputSystem::pendingDeltaY_ = 0;
int32_t    InputSystem::pendingScroll_ = 0;

bool InputState::isKeyDown(Key k) const {
    return curr_[static_cast<uint8_t>(k)];
}

bool InputState::isKeyPressed(Key k) const {
    const uint8_t idx = static_cast<uint8_t>(k);
    return curr_[idx] && !prev_[idx];
}

bool InputState::isKeyReleased(Key k) const {
    const uint8_t idx = static_cast<uint8_t>(k);
    return !curr_[idx] && prev_[idx];
}

bool InputSystem::registerRawInput(void* hwnd) {
    RAWINPUTDEVICE rid[2];
    // Mouse
    rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid[0].usUsage     = HID_USAGE_GENERIC_MOUSE;
    rid[0].dwFlags     = RIDEV_INPUTSINK;
    rid[0].hwndTarget  = static_cast<HWND>(hwnd);
    // Keyboard
    rid[1].usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid[1].usUsage     = HID_USAGE_GENERIC_KEYBOARD;
    rid[1].dwFlags     = RIDEV_INPUTSINK;
    rid[1].hwndTarget  = static_cast<HWND>(hwnd);
    return RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)) == TRUE;
}

void InputSystem::processRawInput(void* hRawInput) {
    UINT size = 0;
    GetRawInputData(static_cast<HRAWINPUT>(hRawInput), RID_INPUT,
                    nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size == 0) return;

    RAWINPUT* raw = nullptr;
    uint8_t   stackBuf[256];
    std::unique_ptr<uint8_t[]> heapBuf;
    if (size <= sizeof(stackBuf)) {
        raw = reinterpret_cast<RAWINPUT*>(stackBuf);
    } else {
        heapBuf = std::make_unique<uint8_t[]>(size);
        raw = reinterpret_cast<RAWINPUT*>(heapBuf.get());
    }

    GetRawInputData(static_cast<HRAWINPUT>(hRawInput), RID_INPUT,
                    raw, &size, sizeof(RAWINPUTHEADER));

    if (raw->header.dwType == RIM_TYPEMOUSE) {
        pendingDeltaX_ += raw->data.mouse.lLastX;
        pendingDeltaY_ += raw->data.mouse.lLastY;

        if (raw->data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
            pendingScroll_ += static_cast<int16_t>(raw->data.mouse.usButtonData);

        // Mouse button state (direct VK values — same array as keyboard)
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
            state_.curr_[static_cast<uint8_t>(Key::MouseLeft)]   = true;
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
            state_.curr_[static_cast<uint8_t>(Key::MouseLeft)]   = false;
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
            state_.curr_[static_cast<uint8_t>(Key::MouseRight)]  = true;
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
            state_.curr_[static_cast<uint8_t>(Key::MouseRight)]  = false;
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
            state_.curr_[static_cast<uint8_t>(Key::MouseMiddle)] = true;
        if (raw->data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
            state_.curr_[static_cast<uint8_t>(Key::MouseMiddle)] = false;
    } else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        const auto& kb = raw->data.keyboard;
        const bool pressed = !(kb.Flags & RI_KEY_BREAK);
        const uint16_t vk  = kb.VKey;
        if (vk < 256) state_.curr_[vk] = pressed;
    }
}

void InputSystem::update() {
    state_.prev_             = state_.curr_;
    state_.mouseDeltaX_      = pendingDeltaX_;
    state_.mouseDeltaY_      = pendingDeltaY_;
    state_.mouseScrollDelta_ = pendingScroll_;
    pendingDeltaX_ = pendingDeltaY_ = pendingScroll_ = 0;
}

} // namespace engine::core
