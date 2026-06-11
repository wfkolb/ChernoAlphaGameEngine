#pragma once
#include <networking/InputMessage.h>
#include <core/input/InputFrame.h>

namespace engine::networking {

// Maps InputFrame (in-process) to InputMessage (wire format).
// moveX/moveZ floats [-1, 1] are quantized to int8_t [-127, 127].
// yaw/pitch deltas (float degrees) are quantized to int16_t (1/100 degree units).
// Button bits: bit0=jump(digitalHeld bit0), bit1=fire(bit1), bit2=crouch(bit2), bit3=interact(bit3).
InputMessage toWire(const core::input::InputFrame& frame, uint32_t tick);

} // namespace engine::networking
