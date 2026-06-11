#include <networking/InputMessageConvert.h>
#include <algorithm>
#include <cmath>

namespace engine::networking {

namespace {

// Clamp a float to int8_t range [-127, 127] and cast.
int8_t quantizeAxis(float v) noexcept {
    const float scaled = v * 127.0f;
    const float clamped = std::fmax(-127.0f, std::fmin(127.0f, scaled));
    return static_cast<int8_t>(clamped);
}

// Clamp a float (degrees) to int16_t range after multiplying by 100.
int16_t quantizeDegrees(float degrees) noexcept {
    const float scaled = degrees * 100.0f;
    constexpr float kMin = static_cast<float>(INT16_MIN);
    constexpr float kMax = static_cast<float>(INT16_MAX);
    const float clamped = std::fmax(kMin, std::fmin(kMax, scaled));
    return static_cast<int16_t>(clamped);
}

} // namespace

InputMessage toWire(const core::input::InputFrame& frame, uint32_t tick) {
    InputMessage msg;
    msg.tick       = tick;
    msg.moveX      = quantizeAxis(frame.moveX);
    msg.moveZ      = quantizeAxis(frame.moveZ);
    msg.yawDelta   = quantizeDegrees(frame.lookYawDelta);
    msg.pitchDelta = quantizeDegrees(frame.lookPitchDelta);

    // Map digital action bitmask bits 0-3 to buttons byte.
    // bit0=jump, bit1=fire, bit2=crouch, bit3=interact
    msg.buttons = static_cast<uint8_t>(frame.digitalHeld & 0x0Fu);

    // fireSerial: count fire-button presses using justPressed bit1 as a
    // per-message wrapping counter; caller is responsible for incrementing.
    // Here we leave it at 0 — the ring-buffer layer will manage it.
    msg.fireSerial = 0u;

    return msg;
}

} // namespace engine::networking
